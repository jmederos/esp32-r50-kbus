#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"

#include "btstack.h"
#include "btstack_port_esp32.h"
#include "bt_services.h"
#include "avrcp_control.h"

#include "bt_commands.h"

#define BT_CMD_TASK_PRIORITY configMAX_PRIORITIES-8

#define HERTZ(hz) ((1000/hz)/portTICK_RATE_MS)

#define ANNOUNCE_STR        CONFIG_BT_ANNOUNCE_STR
#define SHOULD_AUTOCONNECT  CONFIG_BT_AUTOCONNECT

#if SHOULD_AUTOCONNECT
    #define AUTOCONNECT_ADDR    CONFIG_BT_AUTOCONNECT_ADDR
#else
    #define AUTOCONNECT_ADDR "00:00:00:00:00:00"
#endif

static const char* TAG = "bt-services";
static QueueHandle_t bt_cmd_queue;

static void setup_cmd_task();
static void bt_cmd_task();

int bluetooth_services_setup(QueueHandle_t bluetooth_queue){    
    // optional: enable packet logger
    // hci_dump_open(NULL, HCI_DUMP_STDOUT);

    // Configure BTstack for ESP32 VHCI Controller
    btstack_init();

    // Initialize L2CAP
    l2cap_init();
    // Initialize SDP
    sdp_init();
    // Setup avrcp ↙↙↙announce_str  ↙↙↙autoconnect device address
    avrcp_setup(ANNOUNCE_STR, AUTOCONNECT_ADDR); //TODO: store in NVS for dynamic configuration w/web server
    
    // Setup bt command handler
    bt_cmd_queue = bluetooth_queue;
    setup_cmd_task();

    // Turn on bluetooth
    ESP_LOGI(TAG, "Bluetooth HCI on");
    hci_power_control(HCI_POWER_ON);

    //? In case web + bt don't work simulatneously, maybe add SSP or BTLE service for config instead

    #if SHOULD_AUTOCONNECT //If autoconnect is enabled, attempt to connect right after hci power on
        avrcp_ctl_connect();
    #endif

    return 0;
}

static void setup_cmd_task() {
    int tsk_ret = xTaskCreatePinnedToCore(bt_cmd_task, "bt_cmd", 2048, NULL, BT_CMD_TASK_PRIORITY, NULL, 0);
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
                    if(avrcp_get_now_playing() != ERROR_CODE_SUCCESS) {
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
