// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// esp-idf includes
#include "esp_system.h"
#include "esp_log.h"

// component includes
#include "btstack.h"
#include "btstack_port_esp32.h"

#include "bt_services.h"
#include "avrcp_control_driver.h"

#include "bt_common.h"

#define SECONDS(sec) ((sec*1000) / portTICK_RATE_MS)

#define BT_TASK_PRIORITY    configMAX_PRIORITIES-8
#define AUTOCON_TASK_PRIORITY BT_TASK_PRIORITY-10
#define ANNOUNCE_STR        CONFIG_BT_ANNOUNCE_STR
#define SHOULD_AUTOCONNECT  CONFIG_BT_AUTOCONNECT

#if SHOULD_AUTOCONNECT
    #define AUTOCONNECT_ADDR    CONFIG_BT_AUTOCONNECT_ADDR
    #define MAX_CONN_RETRIES    CONFIG_BT_MAX_RETRIES
#endif

static const char* TAG = "bt-services";
static QueueHandle_t bt_cmd_queue;
static QueueHandle_t bt_info_queue;
static TaskHandle_t avrcp_notification_task;

static bt_now_playing_info_t cur_track_info;

static void setup_cmd_task();
static void bt_cmd_task();

static void setup_notify_task();
static void avrcp_notify_task();

int bluetooth_services_setup(QueueHandle_t command_queue, QueueHandle_t info_queue) {
    // optional: enable packet logger
    // hci_dump_open(NULL, HCI_DUMP_STDOUT);

    // Configure BTstack for ESP32 VHCI Controller
    btstack_init();

    // Initialize L2CAP
    l2cap_init();
    // Initialize SDP
    sdp_init();

#if SHOULD_AUTOCONNECT
    setup_notify_task();
    //                                  Setup avrcp ↙↙↙announce_str  ↙↙↙autoconnect device address
    avrcp_setup_with_addr_and_notify(ANNOUNCE_STR, AUTOCONNECT_ADDR, avrcp_notification_task); //TODO: store in NVS for dynamic configuration w/web server
#else      
    // Setup avrcp ↙↙↙announce_str
    avrcp_setup(ANNOUNCE_STR); //TODO: store in NVS for dynamic configuration w/web server
#endif

    // Setup bt command handler
    bt_cmd_queue = command_queue;
    setup_cmd_task();

    bt_info_queue = info_queue;

    // Turn on bluetooth
    ESP_LOGI(TAG, "Bluetooth HCI on");
    hci_power_control(HCI_POWER_ON);

    //? In case web + bt don't work simulatneously, maybe add SSP or BTLE service for config instead
    return 0;
}

static void setup_notify_task() {
    int tsk_ret = xTaskCreatePinnedToCore(avrcp_notify_task, "bt_auto_con", 4096, NULL, AUTOCON_TASK_PRIORITY, &avrcp_notification_task, 0);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "bt_auto_con creation failed with: %d", tsk_ret);}
}

static void avrcp_notify_task() {
    static const char* TASK_TAG = "bt_auto_con";
    uint32_t avrcp_status = 0;
    uint16_t retry_delay = 1;
    uint8_t cxn_attempt_count = 0;

    ESP_LOGD(TASK_TAG, "Autoconnect task started. Blocking while BT boots...");
    vTaskDelay(SECONDS(10)); // Block for 10 seconds while bt boots...

    while(1) {
        ESP_LOGD(TASK_TAG, "Waiting for AVRCP Notification...");
        xTaskNotifyWait(0x00000000, 0x0000000C, &avrcp_status, portMAX_DELAY); // CLear 0x04 && 0x08 Flags

        ESP_LOGD(TASK_TAG, "Notification Receieved 0x%08x", avrcp_status);

        // avrcp_did_init bit set
        if(avrcp_status & 0x01) {

            // if avrcp connected flag is set
            if( (avrcp_status & 0x02) ) {
                // Reset counter and delay
                cxn_attempt_count = 0;
                retry_delay = 1;

                ESP_LOGI(TASK_TAG, "Successfully connected to %s", AUTOCONNECT_ADDR);
            } else if((cxn_attempt_count < MAX_CONN_RETRIES)) { // Otherwise, connected flag is unset; let's check cxn_attempts and retry.

                ESP_LOGI(TASK_TAG, "Attempting bt autoconnect: %02d/%02d", cxn_attempt_count + 1, MAX_CONN_RETRIES);
                
                vTaskDelay(SECONDS(retry_delay));
                avrcp_ctl_connect();
                cxn_attempt_count++;

                // Every 3 connection attempts increase delay
                if((cxn_attempt_count % 3) == 0) retry_delay = (retry_delay * retry_delay) + 1;
            }

            if(avrcp_status & 0x04) {   // Track changed, request "now playing"
                ESP_LOGI(TASK_TAG, "Track Changed...");
                avrcp_req_now_playing();
            }

            if(avrcp_status & 0x08) {   // Track info updated, pull it
                strcpy(cur_track_info.track_title, avrcp_get_track_str());
                strcpy(cur_track_info.album_name, avrcp_get_album_str());
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
                xQueueSend(bt_info_queue, &cur_track_info, 100);
            }
        }
    }
}

static void setup_cmd_task() {
    int tsk_ret = xTaskCreatePinnedToCore(bt_cmd_task, "bt_cmd", 2048, NULL, BT_TASK_PRIORITY, NULL, 0);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "bt_cmd creation failed with: %d", tsk_ret);}
}

static void bt_cmd_task() {
    bt_cmd_type_t command = BT_CMD_NOOP;

    while(1) {
        command = BT_CMD_NOOP;

        if(xQueueReceive(bt_cmd_queue, (void * )&command,  (portTickType)portMAX_DELAY)) {    
            switch(command) {
                case BT_CONNECT:
                    ESP_LOGD(TAG, "BT Attempting Connect");
                    if(avrcp_ctl_connect() != ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP Connection error");
                    }
                    break;
                case BT_DISCONNECT:
                    ESP_LOGD(TAG, "BT Attempting Disconnect");
                    if(avrcp_ctl_disconnect() != ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP Disconnect error");
                    }
                    break;
                case AVRCP_PLAY:
                    ESP_LOGD(TAG, "BT Play Requested");
                    if(avrcp_ctl_play()!= ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP Play command error");
                    }
                    break;
                case AVRCP_PAUSE:
                    ESP_LOGD(TAG, "BT Pause Requested");
                    if(avrcp_ctl_pause() != ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP Pause command error");
                    }
                    break;
                case AVRCP_STOP:
                    ESP_LOGD(TAG, "BT STOP Requested");
                    if(avrcp_ctl_stop() != ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP Stop command error");
                    }
                    break;
                case AVRCP_NEXT:
                    ESP_LOGD(TAG, "BT Next Requested");
                    if(avrcp_ctl_next() != ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP Next command error");
                    }
                    break;
                case AVRCP_PREV:
                    ESP_LOGD(TAG, "BT Previous Requested");
                    if(avrcp_ctl_prev() != ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP Previous command error");
                    }
                    break;
                case AVRCP_FF_START:
                    ESP_LOGD(TAG, "BT Fast Forward Requested");
                    if(avrcp_ctl_start_ff() != ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP FF command error");
                    }
                    break;
                case AVRCP_FF_STOP:
                    ESP_LOGD(TAG, "BT Fast Forward Stop");
                    if(avrcp_ctl_end_long_press() != ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP FF Stop command error");
                    }
                    break;
                case AVRCP_RWD_START:
                    ESP_LOGD(TAG, "BT Rewind Requested");
                    if(avrcp_ctl_start_rwd() != ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP RWD command error");
                    }
                    break;
                case AVRCP_RWD_STOP:
                    ESP_LOGD(TAG, "BT Rewind Stop");
                    if(avrcp_ctl_end_long_press() != ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP RWD command error");
                    }
                    break;
                case AVRCP_GET_INFO:
                ESP_LOGD(TAG, "AVRCP Requesting Track Info");
                    if(avrcp_req_now_playing() != ERROR_CODE_SUCCESS) {
                        ESP_LOGE(TAG, "AVRCP RWD command error");
                    }
                    break;
                default:
                    ESP_LOGD(TAG, "No action registered for command 0x%02x", command);
            }
        } else {
            xQueueReset(bt_cmd_queue); // flush queue
        }
    }
}
