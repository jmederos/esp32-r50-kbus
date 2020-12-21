#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"

#include "kbus_uart_driver.h"
#include "kbus_service.h"
#include "kbus_defines.h"

#include "bt_commands.h"

#define HERTZ(hz) ((1000/hz) / portTICK_RATE_MS)
#define SECONDS(sec) ((sec*1000) / portTICK_RATE_MS)
#define KBUS_TASK_PRIORITY 20
#define CDC_EMU_PRIORITY 7

static const char* TAG = "kbus_service";
static QueueHandle_t bt_cmd_queue;
static QueueHandle_t kbus_rx_queue;
static QueueHandle_t kbus_tx_queue;

static void send_dev_ready_startup();
static void kbus_rx_task();
static void cdc_emulator(kbus_message_t rx_msg);
static void mfl_handler(uint8_t mfl_cmd[2]);

void init_kbus_service(QueueHandle_t bluetooth_queue) {
    bt_cmd_queue = bluetooth_queue;
    kbus_rx_queue = xQueueCreate(4, sizeof(kbus_message_t));
    kbus_tx_queue = xQueueCreate(2, sizeof(kbus_message_t));

    int tsk_ret = xTaskCreatePinnedToCore(kbus_rx_task, "kbus_rx_tsk", 4096, NULL, KBUS_TASK_PRIORITY, NULL, 1);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "kbus_rx_task creation failed with: %d", tsk_ret);}
    init_kbus_uart_driver(kbus_rx_queue, kbus_tx_queue);

    send_dev_ready_startup();
}

static void send_dev_ready_startup() {
    //* CDC -> LOC "Device status Ready After Reset"
    kbus_message_t startup_msg = {
        .src = CDC,
        .dst = LOC,
        .body = {DEV_STAT_RDY, 0x01},
        .body_len = 2
    };

    ESP_LOGD(TAG, "Queueing CD Changer Startup Message");
    xQueueSend(kbus_tx_queue, &startup_msg, 10);

    // other emulated device startup messages here...
}

static void kbus_rx_task() {
    kbus_message_t message;
    while(1) {
        if(xQueueReceive(kbus_rx_queue, (void * )&message,  (portTickType)portMAX_DELAY)) {
            ESP_LOGD(TAG, "data from driver:");
            ESP_LOGD(TAG, "KBUS\t0x%02x -> 0x%02x", message.src, message.dst);
            ESP_LOG_BUFFER_HEXDUMP(TAG, message.body, message.body_len, ESP_LOG_DEBUG);

            switch(message.dst) {
                case LOC:
                    ESP_LOGW(TAG, "Broadcast Message Received!");
                    ESP_LOG_BUFFER_HEXDUMP(TAG, message.body, message.body_len, ESP_LOG_WARN);
                    cdc_emulator(message);
                    break;
                case CDC:
                    ESP_LOGI(TAG, "Message for CD Changer Received");
                    ESP_LOG_BUFFER_HEXDUMP(TAG, message.body, message.body_len, ESP_LOG_INFO);
                    cdc_emulator(message);
                    break;
                default:
                    break;
            }

            switch(message.src) {
                case IKE:
                    ESP_LOGV(TAG, "IKE -> 0x%02x Message Received", message.dst);
                    break;
                case RAD:
                    ESP_LOGV(TAG, "Radio -> 0x%02x Message Received", message.dst);
                    break;
                case MFL:
                    ESP_LOGI(TAG, "MFL -> 0x%02x Message Received", message.dst);
                    mfl_handler((uint8_t[2]){message.body[0], message.body[1]});
                    break;
                default:
                    ESP_LOGV(TAG, "Message Received from 0x%02x", message.src);
                    break;
            }
        }
    }
}

static void cdc_emulator(kbus_message_t rx_msg) {
    //* CD Changer Messages from http://web.archive.org/web/20110320053244/http://ibus.stuge.se/CD_Changer
    kbus_message_t cdc_tx = {   // CDC -> RAD "Device Status Request" response "Device Status Ready"
        .src = CDC,
        .dst = rx_msg.src,      // Address to sender of received msg
        .body_len = 0           // Dunno how long reply is going to be
    };

    switch(rx_msg.body[0]) {
        case DEV_STAT_REQ:
            ESP_LOGD(TAG, "CDC Received: DEVICE STATUS REQUEST");
            cdc_tx.body[0] = DEV_STAT_RDY;
            cdc_tx.body_len = 1;
            xQueueSend(kbus_tx_queue, &cdc_tx, 10);
            ESP_LOGD(TAG, "CDC Queued: DEVICE STATUS READY");
            break;

        case CD_CTRL_REQ:
            ESP_LOGD(TAG, "CDC Received: CD CONTROL REQUEST");
            cdc_tx.body[0] = CD_STAT_RPLY;
            // TODO: Implement
            break;

        default:
            ESP_LOGD(TAG, "CDC Received Other Command:");
            ESP_LOG_BUFFER_HEXDUMP(TAG, rx_msg.body, rx_msg.body_len, ESP_LOG_DEBUG);
    }
}

static void mfl_handler(uint8_t mfl_cmd[2]) {
    //? Queue might be overkill for keeping last command
    static uint8_t *last_mfl_cmd = NULL;
    bt_cmd_type_t bt_command = BT_CMD_NOOP;

    inline void malloc_last_mfl() {
        if(last_mfl_cmd == NULL) {
            ESP_LOGD(TAG, "allocating last_mfl_cmd...");
            last_mfl_cmd = (uint8_t*) malloc(2 * sizeof(uint8_t));
        }
    }

    inline void free_last_mfl() {
        if(last_mfl_cmd) {
            ESP_LOGD(TAG, "freeing last_mfl_cmd...");
            free(last_mfl_cmd);
            last_mfl_cmd = NULL;
        }   
    }

// TODO: Addresses lend themselves to bit twiddling stuff instead of this. Look into it in a revision ☜(ﾟヮﾟ☜)
    switch(mfl_cmd[0]) { //? State machine?...
        case 0x3B:
            ESP_LOGI(TAG, "MFL -> RAD/TEL Button Event");
            
            if(last_mfl_cmd){
                ESP_LOGI(TAG, "last_mfl_cmd: 0x%02x 0x%02x", last_mfl_cmd[0], last_mfl_cmd[1]);
            } else {
                ESP_LOGI(TAG, "last_mfl_cmd: NULL");
            }
            malloc_last_mfl();

            switch(mfl_cmd[1]) {
                //* A button down event received, let's store it.
                case 0x01: // "search up pressed"
                case 0x02: // "R/T pressed"
                case 0x08: { // "search down pressed"
                    ESP_LOGD(TAG, "MFL Up or Down short press");
                    last_mfl_cmd[0] = mfl_cmd[0];
                    last_mfl_cmd[1] = mfl_cmd[1];
                    return; // Return, don't need to take any further action other than storing opening event.
                }

                //* A long press event
                case 0x12: // "R/T pressed long"
                    ESP_LOGD(TAG, "MFL R/T long press");
                    if(last_mfl_cmd[1] == mfl_cmd[1]){
                        return; // Return, this is a repeat long press event.
                    }
                    last_mfl_cmd[0] = mfl_cmd[0];
                    last_mfl_cmd[1] = mfl_cmd[1];
                    bt_command = AVRCP_PLAY;
                    break;
                case 0x11: // "search up pressed long"
                    ESP_LOGD(TAG, "MFL Up long press");
                    if(last_mfl_cmd[1] == mfl_cmd[1]){
                        return; // Return, this is a repeat long press event.
                    }
                    last_mfl_cmd[0] = mfl_cmd[0];
                    last_mfl_cmd[1] = mfl_cmd[1];
                    bt_command = AVRCP_FF_START;
                    break;
                case 0x18: // "search down pressed long"
                    ESP_LOGD(TAG, "MFL Down long press");
                    if(last_mfl_cmd[1] == mfl_cmd[1]){
                        return; // Return, this is a repeat long press event.
                    }
                    last_mfl_cmd[0] = mfl_cmd[0];
                    last_mfl_cmd[1] = mfl_cmd[1];
                    bt_command = AVRCP_RWD_START;
                    break;

                //* A button up event, let's check previous state.
                case 0x21: // "search up released"
                case 0x22: // "R/T released"
                case 0x28: // "search down released"
                    ESP_LOGD(TAG, "MFL arrow button released");
                    if(last_mfl_cmd[0] == mfl_cmd[0]){
                        ESP_LOGD(TAG, "Last BT command matches");
                        switch(last_mfl_cmd[1]) {
                            case 0x01:
                                bt_command = AVRCP_NEXT;
                                break;
                            case 0x02:
                                bt_command = AVRCP_STOP;
                                break;
                            case 0x11:
                                bt_command = AVRCP_FF_STOP;
                                break;
                            case 0x12:
                                // NOOP: Long press AVRCP_PLAY handled during previous event
                                break;
                            case 0x08:
                                bt_command = AVRCP_PREV;
                                break;
                            case 0x18:
                                bt_command = AVRCP_RWD_STOP;
                                break;
                            default:
                                ESP_LOGW(TAG, "Mismatched previous event! Expected 0x%02x, got 0x%02x", last_mfl_cmd[1], mfl_cmd[1]);
                                free_last_mfl();
                                break;
                        }
                    }
                    free_last_mfl(); // Button was released, free last_mfl_cmd
                    break;
                
                default:
                    ESP_LOGI(TAG, "Other MFL -> RAD/TEL Button Event: 0x%02x", mfl_cmd[1]);
                    free_last_mfl(); // Command we don't care about, free last_mfl_cmd
                    break;
            }
            break;
        default:
            ESP_LOGI(TAG, "Other MFL Button Event: 0x%02x 0x%02x", mfl_cmd[0], mfl_cmd[1]);
            free_last_mfl(); // last_mfl_cmd should't have been allocated, but try to free anyway
            break;
    }

    if(bt_command != BT_CMD_NOOP) { // Only put command on queue if it's a valid one
        ESP_LOGI(TAG, "Sending BT Command 0x%02x", bt_command);
        xQueueSend(bt_cmd_queue, &bt_command, 100);
    }
}
