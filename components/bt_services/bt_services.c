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

#define BT_CMD_TASK_PRIORITY 10

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
    // Setup avrcp
    avrcp_setup();

    // Setup bt command handler
    bt_cmd_queue = bluetooth_queue;
    setup_cmd_task();

    // Turn on bluetooth
    ESP_LOGI(TAG, "Bluetooth HCI on");
    hci_power_control(HCI_POWER_ON);

    return 0;
}

static void setup_cmd_task() {
    int tsk_ret = xTaskCreate(bt_cmd_task, "bt_cmd_tsk", 2048, NULL, BT_CMD_TASK_PRIORITY, NULL);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "bt_cmd_tsk creation failed with: %d", tsk_ret);}
}

static void bt_cmd_task() {
    bt_cmd_type_t command = BT_CMD_NOOP;

    while(1) {
        command = BT_CMD_NOOP;

        if(xQueueReceive(bt_cmd_queue, (void * )&command,  (portTickType)portMAX_DELAY)) {    
            switch(command) {
                case AVRCP_NEXT:
                    ESP_LOGD(TAG, "BT Next Requested");
                    avrcp_ctl_next();
                    break;
                case AVRCP_PREV:
                    ESP_LOGD(TAG, "BT Previous Requested");
                    avrcp_ctl_prev();
                    break;
                case AVRCP_FF_START:
                    ESP_LOGD(TAG, "BT Fast Forward Requested");
                    avrcp_ctl_start_ff();
                    break;
                case AVRCP_FF_STOP:
                    ESP_LOGD(TAG, "BT Fast Forward Stop");
                    avrcp_ctl_end_long_press();
                    break;
                case AVRCP_RWD_START:
                    ESP_LOGD(TAG, "BT Rewind Requested");
                    avrcp_ctl_start_rwd();
                    break;
                case AVRCP_RWD_STOP:
                    ESP_LOGD(TAG, "BT Rewind Stop");
                    avrcp_ctl_end_long_press();
                    break;
                default:
                    ESP_LOGD(TAG, "BT command recieved 0x%02x", command);
            }
        }
    }
}
