// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// esp-idf includes
#include "esp_system.h"
#include "esp_log.h"
#include "esp_peripheral.h"
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
#define SHOULD_AUTOCONNECT  CONFIG_BT_AUTOCONNECT

#if SHOULD_AUTOCONNECT
    #define AUTOCONNECT_ADDR    CONFIG_BT_AUTOCONNECT_ADDR
    #define MAX_CONN_RETRIES    CONFIG_BT_MAX_RETRIES
#endif

static const char* TAG = "bt-services";
static QueueHandle_t media_cmd_queue;
static QueueHandle_t media_info_queue;
static TaskHandle_t avrcp_notification_task;

static media_now_playing_t cur_track_info;

static void setup_cmd_task();
static void bt_cmd_task();
static void periodic_timer_callback(void* arg);
esp_timer_handle_t periodic_timer;

const esp_timer_create_args_t periodic_timer_args = {
    .callback = &periodic_timer_callback,
    /* name is optional, but may help identify the timer when debugging */
    .name = "ble-timer"
};

struct data_source_buffer {
    uint8_t buffer[1024];
    uint16_t len;
};

static struct data_source_buffer ancs_buffer = {0};
static uint16_t MTU_size = 23;

uint16_t shared_e_handle;
uint16_t shared_conn_handle;

bool ancs_setup_complete = false;
bool ams_setup_complete = false;

static int ble_ancs_gap_event(struct ble_gap_event *event, void *arg);
static uint8_t own_addr_type;

void ble_store_config_init(void);

int find_ancs_ams(uint16_t conn_handle){

    int rc = 0;

    if (!ams_setup_complete) {
        // Discover AMS
        rc = ble_gattc_disc_svc_by_uuid(conn_handle, &apple_media_service_uuid.u, ams_service_discovered_cb, NULL);
        if (rc != 0) {
            MODLOG_DFLT(ERROR, "BLE_GAP_EVENT_ENC_CHANGE ERROR: 0x%04x", rc);
            return rc;
        }
    }

    if (!ancs_setup_complete && ams_setup_complete) {
        // Discover ANCS
        rc = ble_gattc_disc_svc_by_uuid(conn_handle, &apple_nc_service_uuid.u, ancs_service_discovered_cb, NULL);
        if (rc != 0) { 
            MODLOG_DFLT(ERROR, "BLE_GAP_EVENT_ENC_CHANGE ERROR: 0x%04x", rc);
            return rc;
        }
    }
    return 0;
}

static void periodic_timer_callback(void* arg)
{
    esp_timer_stop(periodic_timer);
    if (ancs_buffer.len > 0) {
        ancs_receive_apple_data_source(ancs_buffer.buffer, ancs_buffer.len);
        memset(ancs_buffer.buffer, 0, ancs_buffer.len);
        ancs_buffer.len = 0;
    }
}

#if NIMBLE_BLE_CONNECT
/**
 * Logs information about a connection to the console.
 */
static void ble_main_print_conn_desc(struct ble_gap_conn_desc *desc) {
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
                "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}
#endif

/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void ble_main_advertise(void) {
    ESP_LOGI(TAG, "ble_main_advertise");
    
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
    ble_main_advertise();
}

void ble_main_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int bluetooth_services_setup(QueueHandle_t command_queue, QueueHandle_t info_queue) {

    int rc;

    /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init nimble %d ", ret);
        return ret;
    }
    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = ble_main_on_reset;
    ble_hs_cfg.sync_cb = ble_main_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_bonding = 1;
    /* Enable the appropriate bit masks to make sure the keys
     * that are needed are exchanged
     */
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_sc = 0;

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("Mini Cooper");
    assert(rc == 0);

    /* XXX Need to have template for store */
    ble_store_config_init();
    nimble_port_freertos_init(ble_main_host_task);

    // Setup bt command handler
    media_cmd_queue = command_queue;
    media_info_queue = info_queue;
    setup_cmd_task();

    return 0;
}

#if SHOULD_AUTOCONNECT
static void setup_notify_task() {
    int tsk_ret = xTaskCreatePinnedToCore(avrcp_notify_task, "bt_auto_con", 4096, NULL, AUTOCON_TASK_PRIORITY, &avrcp_notification_task, 0);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "bt_auto_con creation failed with: %d", tsk_ret);}
}

static void avrcp_notify_task() {
    static const char* TASK_TAG = "bt_auto_con";
    uint32_t avrcp_status = 0;
    uint16_t retry_delay = 1;
    uint16_t cxn_attempt_count = 0;

    ESP_LOGD(TASK_TAG, "Autoconnect task started. Blocking while BT boots...");
    vTaskDelay(SECONDS(10)); // Block for 10 seconds while bt boots...

    while(1) {
        ESP_LOGD(TASK_TAG, "Waiting for AVRCP Notification...");
        xTaskNotifyWait(0x00000000, 0x0000FF0C, &avrcp_status, portMAX_DELAY); // Clear 0x04 (New Track), 0x08 (Updated Metadata) Flags, 0xFF00 (play status)
        ESP_LOGD(TASK_TAG, "Notification Receieved 0x%08x", avrcp_status);

        // avrcp_did_init bit set
        if(avrcp_status & 0x01) {

            // if avrcp connected flag is set
            if( (avrcp_status & 0x02) ) {
                if(cxn_attempt_count > 0){
                    ESP_LOGI(TASK_TAG, "Successfully connected to %s", AUTOCONNECT_ADDR);
                    // Clear out track info
                    strcpy(cur_track_info.track_title, "");
                    strcpy(cur_track_info.album_name,  "");
                    strcpy(cur_track_info.artist_name, "");
                    // Set state details
                    cur_track_info.state = DEV_CONNECTED;
                    sprintf(cur_track_info.state_detail, "%cConnected%c",RT_STRIPE_TRI, LT_STRIPE_TRI);
                    // Put "track data" on queue
                    xQueueSend(media_info_queue, &cur_track_info, 0);
                    // Reset counter and delay
                    cxn_attempt_count = 0;
                    retry_delay = 1;
                }
            } else if((cxn_attempt_count < MAX_CONN_RETRIES)) { // Otherwise, connected flag is unset; let's check cxn_attempts and retry.

                ESP_LOGI(TASK_TAG, "Attempting bt autoconnect: %02d/%02d", cxn_attempt_count + 1, MAX_CONN_RETRIES);
                
                vTaskDelay(SECONDS(retry_delay));
                avrcp_ctl_connect();
                cxn_attempt_count++;

                // Every 2 connection attempts increase by f(x) = 2x; this should yeild increasing delay for first 30sec with 10sec delays thereafter.
                if(((cxn_attempt_count % 2) == 0) && (retry_delay < 11)) retry_delay *= 2;

                // Clear out track info
                strcpy(cur_track_info.track_title, "");
                strcpy(cur_track_info.album_name,  "");
                strcpy(cur_track_info.artist_name, "");
                // Set state details
                cur_track_info.state = DEV_CONNECTING;
                sprintf(cur_track_info.state_detail, "Connecting%c", SIGNAL_1 - (cxn_attempt_count%4));
                // Put "track data" on queue
                xQueueSend(media_info_queue, &cur_track_info, 0);
            } else {
                // Clear out track info
                strcpy(cur_track_info.track_title, "");
                strcpy(cur_track_info.album_name,  "");
                strcpy(cur_track_info.artist_name, "");

                cur_track_info.state = DEV_DISCONNECTED;
                strcpy(cur_track_info.state_detail, "Disconnected");
                
                xQueueSend(media_info_queue, &cur_track_info, 0);
            }

            if(avrcp_status & 0xFF00) {
                media_state_t cur_playback_state = ((avrcp_status & 0x0000FF00) >> 8);
                cur_track_info.state = cur_playback_state;
                ESP_LOGW(TASK_TAG, "Current Playback State 0x%02x", cur_playback_state);
                switch (cur_playback_state) {
                    case MEDIA_STOPPED:
                        sprintf(cur_track_info.state_detail, "Stop  %c", SIGNAL_3);
                        break;
                    case MEDIA_PLAYING:
                        sprintf(cur_track_info.state_detail, "Play  %c", RT_SOLID_TRI);
                        break;
                    case MEDIA_PAUSED:
                        sprintf(cur_track_info.state_detail, "Pause %c", PAUSE_CHAR);
                        break;
                    case MEDIA_SEEK_FWD:
                        sprintf(cur_track_info.state_detail, "Seek %c%c", RT_SOLID_TRI, RT_SOLID_TRI);
                        break;
                    case MEDIA_SEEK_RWD:
                        sprintf(cur_track_info.state_detail, "Seek %c%c", LT_SOLID_TRI, LT_SOLID_TRI);
                        break;
                    default:
                        break;
                }
                xQueueSend(media_info_queue, &cur_track_info, 10);
            }

            if(avrcp_status & 0x08) {   // Track info updated, pull it
                strcpy(cur_track_info.track_title, avrcp_get_track_str());
                strcpy(cur_track_info.album_name,  avrcp_get_album_str());
                strcpy(cur_track_info.artist_name, avrcp_get_artist_str());

                uint16_t cur_track_total_tracks = avrcp_get_track_info();

                cur_track_info.total_tracks = (uint8_t) cur_track_total_tracks & 0x0F;
                cur_track_info.cur_track = (uint8_t) (cur_track_total_tracks >> 8) & 0x0F;

                cur_track_info.track_len_ms = avrcp_get_track_len_ms();

                ESP_LOGI(TASK_TAG, "Track Info: %s - %s - %s\t%d/%d",
                                cur_track_info.track_title,
                                cur_track_info.album_name,
                                cur_track_info.artist_name,
                                cur_track_info.cur_track, cur_track_info.total_tracks);
                xQueueSend(media_info_queue, &cur_track_info, 10);
            }

            if(avrcp_status & 0x04) {   // Track changed, request "now playing"
                ESP_LOGI(TASK_TAG, "Track Changed...");
                avrcp_req_now_playing();
            }
        }
    }
}
#endif

static void setup_cmd_task() {
    int tsk_ret = xTaskCreatePinnedToCore(bt_cmd_task, "bt_cmd", 2048, NULL, BT_TASK_PRIORITY, NULL, 0);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "bt_cmd creation failed with: %d", tsk_ret);}
}

static void bt_cmd_task() {
    media_cmd_type_t command = BT_CMD_NOOP;

    while(1) {
        command = BT_CMD_NOOP;

        if(xQueueReceive(media_cmd_queue, (void*) &command,  portMAX_DELAY) && ams_setup_complete) {    
            switch(command) {
                // case BT_CONNECT:
                //     ESP_LOGD(TAG, "BT Attempting Connect");
                //     if(avrcp_ctl_connect() != ERROR_CODE_SUCCESS) {
                //         ESP_LOGE(TAG, "AVRCP Connection error");
                //     }
                //     break;
                // case BT_DISCONNECT:
                //     ESP_LOGD(TAG, "BT Attempting Disconnect");
                //     if(avrcp_ctl_disconnect() != ERROR_CODE_SUCCESS) {
                //         ESP_LOGE(TAG, "AVRCP Disconnect error");
                //     }
                //     break;
                case MEDIA_PLAY:
                    ESP_LOGD(TAG, "BT Play Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDPlay)) {
                        ESP_LOGE(TAG, "Play command error");
                    }
                    break;
                case MEDIA_PAUSE:
                    ESP_LOGD(TAG, "BT Pause Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDPause)) {
                        ESP_LOGE(TAG, "Pause command error");
                    }
                    break;
                case MEDIA_STOP:
                    ESP_LOGD(TAG, "BT STOP Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDPause)) {
                        ESP_LOGE(TAG, "Pause command error");
                    }
                    break;
                case MEDIA_NEXT:
                    ESP_LOGD(TAG, "BT Next Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDNextTrack)) {
                        ESP_LOGE(TAG, "Next command error");
                    }
                    break;
                case MEDIA_PREV:
                    ESP_LOGD(TAG, "BT Previous Requested");
                    if(ams_send_remote_command(shared_conn_handle, RemoteCommandIDPreviousTrack)) {
                        ESP_LOGE(TAG, "Previous command error");
                    }
                    break;
                // case MEDIA_FF_START:
                //     ESP_LOGD(TAG, "BT Fast Forward Requested");
                //     if(avrcp_ctl_start_ff() != ERROR_CODE_SUCCESS) {
                //         ESP_LOGE(TAG, "AVRCP FF command error");
                //     }
                //     break;
                // case MEDIA_FF_STOP:
                //     ESP_LOGD(TAG, "BT Fast Forward Stop");
                //     if(avrcp_ctl_end_long_press() != ERROR_CODE_SUCCESS) {
                //         ESP_LOGE(TAG, "AVRCP FF Stop command error");
                //     }
                //     break;
                // case MEDIA_RWD_START:
                //     ESP_LOGD(TAG, "BT Rewind Requested");
                //     if(avrcp_ctl_start_rwd() != ERROR_CODE_SUCCESS) {
                //         ESP_LOGE(TAG, "AVRCP RWD command error");
                //     }
                //     break;
                // case MEDIA_RWD_STOP:
                //     ESP_LOGD(TAG, "BT Rewind Stop");
                //     if(avrcp_ctl_end_long_press() != ERROR_CODE_SUCCESS) {
                //         ESP_LOGE(TAG, "AVRCP RWD command error");
                //     }
                //     break;
                // case MEDIA_REQ_INFO:
                // ESP_LOGD(TAG, "AVRCP Requesting Track Info");
                //     if(avrcp_req_now_playing() != ERROR_CODE_SUCCESS) {
                //         ESP_LOGE(TAG, "AVRCP RWD command error");
                //     }
                //     break;
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
            ble_main_print_conn_desc(&desc);
        }
        MODLOG_DFLT(INFO, "\n");

        if (event->connect.status != 0) {
            /* Connection failed; resume advertising. */
            ble_main_advertise();
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
        ble_main_print_conn_desc(&event->disconnect.conn);
        MODLOG_DFLT(INFO, "\n");
        /* Connection terminated; resume advertising. */
        ble_main_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        MODLOG_DFLT(INFO, "connection updated; status=%d ",
                    event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        ble_main_print_conn_desc(&desc);
        MODLOG_DFLT(INFO, "\n");
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        MODLOG_DFLT(INFO, "advertise complete; reason=%d",
                    event->adv_complete.reason);
        ble_main_advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        MODLOG_DFLT(INFO, "encryption change event; status=%d ", event->enc_change.status);

        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);

        ble_main_print_conn_desc(&desc);
        MODLOG_DFLT(INFO, "\n");
        
        shared_conn_handle = event->connect.conn_handle;
        find_ancs_ams(shared_conn_handle);

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

            if (event->notify_rx.om->om_len == (MTU_size - 3)) {
                esp_timer_start_periodic(periodic_timer, 500000);
            } else {
                esp_timer_stop(periodic_timer);
                ancs_receive_apple_data_source(ancs_buffer.buffer, ancs_buffer.len);
                memset(ancs_buffer.buffer, 0, ancs_buffer.len);
                ancs_buffer.len = 0;
            }    
        } else if (event->notify_rx.attr_handle == ams_entity_attr_handle) {
            MODLOG_DFLT(INFO, "Received entity_attr notify_rx event");
            ams_decode_entity_attr(event->notify_rx.om->om_data, event->notify_rx.om->om_len);
        } else if (event->notify_rx.attr_handle == ams_entity_update_handle) {
            ams_decode_entity_update(event->notify_rx.om->om_data, event->notify_rx.om->om_len);
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

        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456; // This is the passkey to be entered on peer
            ESP_LOGI(TAG, "Enter passkey %" PRIu32 "on the peer side", pkey.passkey);
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            ESP_LOGI(TAG, "Passkey on device's display: %" PRIu32 , event->passkey.params.numcmp);
            ESP_LOGI(TAG, "Accept or reject the passkey through console in this format -> key Y or key N");
            pkey.action = event->passkey.params.action;
            if (scli_receive_key(&key)) {
                pkey.numcmp_accept = key;
            } else {
                pkey.numcmp_accept = 0;
                ESP_LOGE(TAG, "Timeout! Rejecting the key");
            }
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
