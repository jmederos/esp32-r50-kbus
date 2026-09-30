// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// esp-idf includes
#include "esp_system.h"
#include "esp_log.h"
#include "esp_peripheral.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "console/console.h"

// nimBLE
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/ble.h"

// BLE
#include "services/gap/ble_svc_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

// component includes
#include "bt_services.h"
#include "media_common.h"
#include "special_chars.h"
#include "ble_ancs.h"
#include "ble_ams.h"

#define SECONDS(sec) ((sec*1000) / portTICK_PERIOD_MS)

#define BT_TASK_PRIORITY    configMAX_PRIORITIES-8
#define AUTOCON_TASK_PRIORITY BT_TASK_PRIORITY-10
#define ANNOUNCE_STR        CONFIG_BT_ANNOUNCE_STR

static const char* TAG = "bt-services";
static QueueHandle_t media_cmd_queue;
static QueueHandle_t media_info_queue;

static media_now_playing_t cur_track_info;
static uint32_t passkey = 0; // Init passkey to 0

static void setup_cmd_task();
static void bt_cmd_task();

struct data_source_buffer {
    uint8_t buffer[1024];
    uint16_t len;
};

static struct data_source_buffer ancs_buffer = {0};
static uint16_t MTU_size = 23;

uint16_t shared_e_handle = 0;
uint16_t shared_conn_handle = 0;

bool ancs_setup_complete = false;
bool ams_setup_complete = false;

static uint8_t own_addr_type;

static int ble_ancs_gap_event(struct ble_gap_event *event, void *arg);
void ble_store_config_init(void);

int find_ancs_ams(uint16_t conn_handle){

    int rc = 0;
    if (!ams_setup_complete) {
        // Discover AMS
        rc = ble_gattc_disc_svc_by_uuid(conn_handle, &apple_media_service_uuid.u, ams_service_discovered_cb, NULL);
        if (rc != 0) {
            MODLOG_DFLT(ERROR, "find_ams ERROR: 0x%04x", rc);
            return rc;
        }
    }
    if (!ancs_setup_complete && ams_setup_complete) {
        // Discover ANCS
        rc = ble_gattc_disc_svc_by_uuid(conn_handle, &apple_nc_service_uuid.u, ancs_service_discovered_cb, NULL);
        if (rc != 0) { 
            MODLOG_DFLT(ERROR, "find_ancs ERROR: 0x%04x", rc);
            return rc;
        }
    }
    return 0;
}

#if NIMBLE_BLE_CONNECT
/**
 * Logs information about a connection to the console.
 */
static void ble_svc_print_conn_desc(struct ble_gap_conn_desc *desc) {
    MODLOG_DFLT(INFO, "handle=%d our_ota_addr_type=%d our_ota_addr=",
                desc->conn_handle, desc->our_ota_addr.type);
    print_addr(desc->our_ota_addr.val);
    MODLOG_DFLT(INFO, " our_id_addr_type=%d our_id_addr=",
                desc->our_id_addr.type);
    print_addr(desc->our_id_addr.val);
    MODLOG_DFLT(INFO, " peer_ota_addr_type=%d peer_ota_addr=",
                desc->peer_ota_addr.type);
    print_addr(desc->peer_ota_addr.val);
    MODLOG_DFLT(INFO, " peer_id_addr_type=%d peer_id_addr=",
                desc->peer_id_addr.type);
    print_addr(desc->peer_id_addr.val);
    MODLOG_DFLT(INFO, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d key_size=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded,
                desc->sec_state.key_size);
}
#endif

/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void ble_svc_advertise(void) {
    ESP_LOGI(TAG, "ble_svc_advertise");
    
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 16-bit service UUIDs (alert notifications).
     */
    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    fields.uuids16 = (ble_uuid16_t[]) {BLE_UUID16_INIT(0x180A), BLE_UUID16_INIT(0x1812)};
    fields.num_uuids16 = 2;
    fields.uuids16_is_complete = 1;

    fields.appearance = 0x03C3;
    fields.appearance_is_present = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    // set flags to false on every advertise
    ancs_setup_complete = false;
    ams_setup_complete  = false;
    // Set to disconnected
    *cur_track_info.state = DEV_DISCONNECTED;

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_ancs_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
}

static void ble_main_on_reset(int reason) {
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

static void ble_main_on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Printing ADDR */
    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    MODLOG_DFLT(INFO, "Device Address: ");
    print_addr(addr_val);
    MODLOG_DFLT(INFO, "\n");
    /* Begin advertising. */
    ble_svc_advertise();
}

void ble_main_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int bluetooth_services_setup(QueueHandle_t command_queue, QueueHandle_t info_queue) {

    int rc;
    esp_err_t ret;

    // Setup bt command handler
    media_cmd_queue = command_queue;
    media_info_queue = info_queue;
    setup_cmd_task();

    // Setup cur_track pointers
    cur_track_info.album_name  = ams_get_album_str();
    cur_track_info.artist_name = ams_get_artist_str();
    cur_track_info.track_title = ams_get_track_str();
    cur_track_info.app_name    = ams_get_app_str();
    cur_track_info.state       = (media_state_t*) ams_get_playback_state();

    // TODO: Randomize passkey on each disconnect/reconnect instead of on boot
    // Initialize random passkey and bitshift
    esp_fill_random(&passkey, 2);
    passkey = passkey >> 2;

    // Initialize to disconnected state
    *cur_track_info.state = DEV_DISCONNECTED;
    sprintf(cur_track_info.album_name, "%s", "");
    sprintf(cur_track_info.artist_name, "%s", "");
    sprintf(cur_track_info.track_title, "%s", "");
    sprintf(cur_track_info.app_name, "No BT cxn");

    // xQueueSend(media_info_queue, &cur_track_info, 0);

    // ? NVS init done in main `````````````````````````````````````````````````````
    /* Initialize NVS — it is used to store PHY calibration data */
    // esp_err_t ret = nvs_flash_init();
    // if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    //     ESP_ERROR_CHECK(nvs_flash_erase());
    //     ret = nvs_flash_init();
    // }
    // ESP_ERROR_CHECK(ret);
    // ? ``````````````````````````````````````````````````````````````````````````

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init nimble %d ", ret);
        return ret;
    }
    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = ble_main_on_reset;
    ble_hs_cfg.sync_cb = ble_main_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_our_key_dist   |= BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set(ANNOUNCE_STR);
    assert(rc == 0);

    /* XXX Need to have template for store */
    ble_store_config_init();
    nimble_port_freertos_init(ble_main_host_task);
    return 0;
}

static void setup_cmd_task() {
    int tsk_ret = xTaskCreatePinnedToCore(bt_cmd_task, "bt_cmd", 2048, NULL, BT_TASK_PRIORITY, NULL, 1);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "bt_cmd creation failed with: %d", tsk_ret);}
}

static void bt_cmd_task() {
    media_cmd_type_t command = BT_CMD_NOOP;

    while(1) {
        command = BT_CMD_NOOP;

        if(xQueueReceive(media_cmd_queue, (void*) &command,  portMAX_DELAY) && ams_setup_complete) {    
            switch(command) {
                case MEDIA_PLAY:
                    ESP_LOGD(TAG, "BT Play Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDPlay)) {
                        ESP_LOGE(TAG, "Play command error: handle 0x%04x", shared_conn_handle);
                    }
                    break;
                case MEDIA_PAUSE:
                    ESP_LOGD(TAG, "BT Pause Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDPause)) {
                        ESP_LOGE(TAG, "Pause command error: handle 0x%04x", shared_conn_handle);
                    }
                    break;
                case MEDIA_STOP:
                    ESP_LOGD(TAG, "BT STOP Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDPause)) {
                        ESP_LOGE(TAG, "Pause command error: handle 0x%04x", shared_conn_handle);
                    }
                    break;
                case MEDIA_NEXT:
                    ESP_LOGD(TAG, "BT Next Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDNextTrack)) {
                        ESP_LOGE(TAG, "Next command error: handle 0x%04x", shared_conn_handle);
                    }
                    break;
                case MEDIA_PREV:
                    ESP_LOGD(TAG, "BT Previous Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDPreviousTrack)) {
                        ESP_LOGE(TAG, "Previous command error: handle 0x%04x", shared_conn_handle);
                    }
                    break;
                case MEDIA_FF_START:
                    ESP_LOGD(TAG, "BT Fast Forward Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDSkipForward)) {
                        ESP_LOGE(TAG, "FF command error: handle 0x%04x", shared_conn_handle);
                    }
                    break;
                case MEDIA_FF_STOP:
                    ESP_LOGD(TAG, "BT Fast Forward Stop");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDPlay)) {
                        ESP_LOGE(TAG, "FF Stop command error: handle 0x%04x", shared_conn_handle);
                    }
                    break;
                case MEDIA_RWD_START:
                    ESP_LOGD(TAG, "BT Rewind Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDSkipBackward)) {
                        ESP_LOGE(TAG, "RWD command error: handle 0x%04x", shared_conn_handle);
                    }
                    break;
                case MEDIA_RWD_STOP:
                    ESP_LOGD(TAG, "BT Rewind Stop");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDPlay)) {
                        ESP_LOGE(TAG, "RWD command error: handle 0x%04x", shared_conn_handle);
                    }
                    break;

                default:
                    ESP_LOGD(TAG, "No action registered for command 0x%02x", command);
            }
        } else {
            xQueueReset(media_cmd_queue); // flush queue
        }
    }
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * ble_ancs uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  ble_ancs.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int ble_ancs_gap_event(struct ble_gap_event *event, void *arg) {
#if NIMBLE_BLE_CONNECT
    struct ble_gap_conn_desc desc;
    int rc;
#endif

    switch (event->type) {

#if NIMBLE_BLE_CONNECT
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        MODLOG_DFLT(INFO, "connection %s; status=%d ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            ble_svc_print_conn_desc(&desc);
        }
        MODLOG_DFLT(INFO, "\n");

        if (event->connect.status != 0) {
            /* Connection failed; resume advertising. */
            ble_svc_advertise();
        }

        /** Initiate security - It will perform
         * Pairing (Exchange keys)
         * Bonding (Store keys)
         * Encryption (Enable encryption)
         * Will invoke event BLE_GAP_EVENT_ENC_CHANGE
         **/
        rc = ble_gap_security_initiate(event->connect.conn_handle);
        if (rc != 0) {
            MODLOG_DFLT(INFO, "Security could not be initiated, rc = %d\n", rc);
            return ble_gap_terminate(event->connect.conn_handle,
                                    BLE_ERR_REM_USER_CONN_TERM);
        } else {
            MODLOG_DFLT(INFO, "Connection secured\n");
        }
    return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
        ble_svc_print_conn_desc(&event->disconnect.conn);
        MODLOG_DFLT(INFO, "\n");
        /* Connection terminated; resume advertising. */
        ble_svc_advertise();
        xQueueSend(media_info_queue, &cur_track_info, 0);
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        MODLOG_DFLT(INFO, "connection updated; status=%d ",
                    event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        ble_svc_print_conn_desc(&desc);
        MODLOG_DFLT(INFO, "\n");
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        MODLOG_DFLT(INFO, "advertise complete; reason=%d",
                    event->adv_complete.reason);
        ble_svc_advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        MODLOG_DFLT(INFO, "encryption change event; status=%d ", event->enc_change.status);

        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);

        ble_svc_print_conn_desc(&desc);
        MODLOG_DFLT(INFO, "\n");
        
        shared_conn_handle = event->connect.conn_handle;
        find_ancs_ams(event->connect.conn_handle);

        // Update Display
        *cur_track_info.state = DEV_CONNECTED;
        sprintf(cur_track_info.app_name, "Connected");
        xQueueSend(media_info_queue, &cur_track_info, 0);

        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        MODLOG_DFLT(INFO, "notify_tx event; conn_handle=%d attr_handle=%d "
                    "status=%d is_indication=%d",
                    event->notify_tx.conn_handle,
                    event->notify_tx.attr_handle,
                    event->notify_tx.status,
                    event->notify_tx.indication);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        MODLOG_DFLT(DEBUG, "notify_rx event; conn_handle=%d attr_handle=%d "
                "status=%d is_indication=%d",
                event->notify_rx.conn_handle,
                event->notify_rx.attr_handle,
                event->notify_rx.indication);

        /* Peer sent us a notification or indication. */
        if (event->notify_rx.attr_handle == ancs_notification_src_handle) {

            ancs_receive_apple_notification_source(event->notify_rx.om->om_data, event->notify_rx.om->om_len);

            uint8_t *notificationUID = &event->notify_rx.om->om_data[4];

            if (event->notify_rx.om->om_data[0] == EventIDNotificationAdded &&
                event->notify_rx.om->om_data[2] == CategoryIDIncomingCall) {
                // MODLOG_DFLT(INFO, "IncomingCall, reject");
                // ancs_perform_notification_action(notificationUID, ActionIDNegative, event->notify_rx.conn_handle, ancs_ctrl_point_handle);
            } else if (event->notify_rx.om->om_data[0] == EventIDNotificationAdded) {
                MODLOG_DFLT(INFO, "Get detailed information");
                ancs_get_notification_attributes(event->notify_rx.conn_handle, ancs_ctrl_point_handle,
                                                notificationUID, sizeof(p_attr)/sizeof(ble_noti_attr_list_t), p_attr);
            }
        } else if (event->notify_rx.attr_handle == ancs_data_src_handle) {
            memcpy(&ancs_buffer.buffer[ancs_buffer.len],
            event->notify_rx.om->om_data,
            event->notify_rx.om->om_len);
            ancs_buffer.len += event->notify_rx.om->om_len;

            // if (event->notify_rx.om->om_len == (MTU_size - 3)) {
            //     esp_timer_start_periodic(periodic_timer, 500000);
            // } else {
            //     esp_timer_stop(periodic_timer);
                ancs_receive_apple_data_source(ancs_buffer.buffer, ancs_buffer.len);
                memset(ancs_buffer.buffer, 0, ancs_buffer.len);
                ancs_buffer.len = 0;
            // }
        } else if (event->notify_rx.attr_handle == ams_entity_attr_handle) {
            MODLOG_DFLT(ERROR, "Received entity_attr notify_rx event");
            ams_decode_entity_attr(event->notify_rx.om->om_data, event->notify_rx.om->om_len);
            xQueueSend(media_info_queue, &cur_track_info, 0);
        } else if (event->notify_rx.attr_handle == ams_entity_update_handle) {
            MODLOG_DFLT(WARN, "Received entity_update notify_rx event");
            ams_decode_entity_update(event->notify_rx.om->om_data, event->notify_rx.om->om_len);
            xQueueSend(media_info_queue, &cur_track_info, 0);
        } else if (event->notify_rx.attr_handle == ams_remote_command_handle) {
            MODLOG_DFLT(INFO, "Received remote_command notify_rx event");
        } else {
            MODLOG_DFLT(INFO, "unknown handle, receive notify value:" );
        }
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        MODLOG_DFLT(INFO, "subscribe event; conn_handle=%d attr_handle=%d "
                    "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                    event->subscribe.conn_handle,
                    event->subscribe.attr_handle,
                    event->subscribe.reason,
                    event->subscribe.prev_notify,
                    event->subscribe.cur_notify,
                    event->subscribe.prev_indicate,
                    event->subscribe.cur_indicate);
        return 0;

    case BLE_GAP_EVENT_MTU:
        MTU_size = event->mtu.value;
        MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(TAG, "PASSKEY_ACTION_EVENT started");
        struct ble_sm_io pkey = {0};
        int key = 0;

        // Update info buffer so passkey is displayed
        *cur_track_info.state = DEV_CONNECTING;

        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = passkey;

            sprintf(cur_track_info.app_name, "%lu", passkey);
            xQueueSend(media_info_queue, &cur_track_info, 0);

            ESP_LOGI(TAG, "Enter passkey %" PRIu32 " on the peer side", pkey.passkey);
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);

        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            ESP_LOGI(TAG, "Passkey on device's display: %" PRIu32 , event->passkey.params.numcmp);
            ESP_LOGI(TAG, "Accept or reject the passkey through console in this format -> key Y or key N");
            pkey.action = event->passkey.params.action;

            sprintf(cur_track_info.app_name, "%lu", event->passkey.params.numcmp);
            xQueueSend(media_info_queue, &cur_track_info, 0);
            // if (scli_receive_key(&key)) {
            //     pkey.numcmp_accept = key;
            // } else {
            //     pkey.numcmp_accept = 0;
            //     ESP_LOGE(TAG, "Timeout! Rejecting the key");
            // }
            pkey.numcmp_accept = passkey; // TODO: logic for y/n input
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);

        } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
            static uint8_t tem_oob[16] = {0};
            pkey.action = event->passkey.params.action;
            for (int i = 0; i < 16; i++) {
                pkey.oob[i] = tem_oob[i];
            }
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);

        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            ESP_LOGI(TAG, "Enter the passkey through console in this format-> key 123456");
            pkey.action = event->passkey.params.action;
            if (scli_receive_key(&key)) {
                pkey.passkey = key;
            } else {
                pkey.passkey = 0;
                ESP_LOGE(TAG, "Timeout! Passing 0 as the key");
            }
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        }
        return 0;

#endif
    }
    return 0;
}
