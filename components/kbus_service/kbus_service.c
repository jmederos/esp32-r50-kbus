// C stdlib includes
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// esp-idf includes
#include "esp_system.h"
#include "esp_log.h"

// component includes
#include "kbus_uart_driver.h"
#include "kbus_service.h"
#include "kbus_defines.h"
#include "bt_commands.h"

// ! Debug Flags
// #define QUEUE_DEBUG

#define HERTZ(hz) ((1000/hz)/portTICK_RATE_MS)
#define SECONDS(sec) ((sec*1000) / portTICK_RATE_MS)
#define KBUS_TASK_PRIORITY configMAX_PRIORITIES-5

static const char* TAG = "kbus_service";
static QueueHandle_t bt_cmd_queue;
static QueueHandle_t kbus_rx_queue; //TODO: Message Buffer instead of Queue... lol, not available on esp-idf 4.0.2; will be on 4.3
static QueueHandle_t kbus_tx_queue; //TODO: See https://github.com/espressif/esp-idf/issues/4945 for details

static void kbus_rx_task();
static void cdc_emulator(kbus_message_t rx_msg);
static void sdrs_emulator(kbus_message_t rx_msg);
static void tel_emulator(kbus_message_t rx_msg);
static void mfl_handler(uint8_t mfl_cmd[2]);
static void display_message(uint8_t cmd, uint8_t layout, uint8_t flags, char* text);
static void display_fuzz_task();

static inline void send_dev_ready(uint8_t source, uint8_t dest, bool startup);

#ifdef QUEUE_DEBUG
static void create_kbus_queue_watcher();
#endif

void init_kbus_service(QueueHandle_t bluetooth_queue) {
    bt_cmd_queue = bluetooth_queue;
    kbus_rx_queue = xQueueCreate(8, sizeof(kbus_message_t));
    kbus_tx_queue = xQueueCreate(4, sizeof(kbus_message_t));

    int tsk_ret = xTaskCreatePinnedToCore(kbus_rx_task, "kbus_rx", 8192, NULL, KBUS_TASK_PRIORITY, NULL, 1);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "kbus_rx creation failed with: %d", tsk_ret);}
    init_kbus_uart_driver(kbus_rx_queue, kbus_tx_queue);

    // tsk_ret = xTaskCreatePinnedToCore(display_fuzz_task, "dsply_fuzz", 4096, NULL, KBUS_TASK_PRIORITY - 5, NULL, 1);
    // if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "dsply_tst creation failed with: %d", tsk_ret);}

#ifdef QUEUE_DEBUG
    create_kbus_queue_watcher();
#endif
    
    //? Make into one-off task? --------------------------------------------
    // We're emulating SDRS, CDC and TEL, send a device startup announcement
    vTaskDelay(SECONDS(5));
    send_dev_ready(SDRS, LOC, true);
    vTaskDelay(500);
    // send_dev_ready(CDC, LOC, true);
    // vTaskDelay(500);
    send_dev_ready(TEL, LOC, true);
    //? -------------------------------------------------------------------
}

static void display_fuzz_task(){
    #define DSPLY_LAYOUT_DELAY 1
    uint8_t display_layout = 0x00;
    char layout_message[32];

    vTaskDelay(SECONDS(80));

    while(display_layout < 0xff){
        sprintf(layout_message, "x23 layout 0x%02x", display_layout);
        display_message(0x23, display_layout, 0x30, layout_message);

        vTaskDelay(SECONDS(2));

        sprintf(layout_message, "x24 layout 0x%02x", display_layout);
        display_message(0x24, display_layout, 0x30, layout_message);

        display_layout++;
        vTaskDelay(SECONDS(DSPLY_LAYOUT_DELAY));
    }

    vTaskDelete(NULL); // Delete task after going through all possible layouts...
}

static inline void send_dev_ready(uint8_t source, uint8_t dest, bool startup) {
    //* SOURCE -> DEST "Device Status Ready"
    kbus_message_t message = {
        .src = source,
        .dst = dest,
        .body = {DEV_STAT_RDY, 0x00},
        .body_len = 2
    };

    // If startup, update message to
    //"Device Status Ready After Reset"
    if(startup) {
        message.body[1] = 0x01;
        ESP_LOGD(TAG, "Queueing 0x%02x -> 0x%02x DEVICE READY AFTER RESET", source, dest);
    } else {
        ESP_LOGD(TAG, "Queueing 0x%02x -> 0x%02x DEVICE READY", source, dest);
    }
    xQueueSend(kbus_tx_queue, &message, (portTickType)portMAX_DELAY);
}

static void kbus_rx_task() {
    kbus_message_t message;
    while(1) {
        if(xQueueReceive(kbus_rx_queue, (void * )&message,  (portTickType)portMAX_DELAY)) {
            ESP_LOGD(TAG, "data from driver:");
            ESP_LOGD(TAG, "KBUS\t0x%02x -> 0x%02x", message.src, message.dst);
            ESP_LOG_BUFFER_HEXDUMP(TAG, message.body, message.body_len, ESP_LOG_DEBUG);

            switch(message.src) {
                case MFL:
                    ESP_LOGD(TAG, "MFL -> 0x%02x Message Received", message.dst);
                    mfl_handler((uint8_t[2]){message.body[0], message.body[1]});
                    break;

                default:
                    break;
            }

            switch(message.dst) {

                case LOC:
                    // Noop right away on LOCAL broadcast messages. short circuit -> save some cycles
                    break;

                case GLO:
                    // If ignition is set to Pos1_ACC, send device startup packet
                    if(message.body_len == 2 && message.body[0] == IGN_STAT_RPLY && message.body[1] == 0x03) {
                        ESP_LOGI(TAG, "Ignition On...");
                    //     // Send AVRCP_PLAY command when ignition_status bits set to: Pos1_Acc Pos2_On
                    //     bt_cmd_type_t bt_command = AVRCP_PLAY;
                    //     xQueueSend(bt_cmd_queue, &bt_command, (portTickType)portMAX_DELAY);
                    }
                    // Only care for this one particular GLOBAL message right now
                    break;

                case SDRS:
                    ESP_LOGD(TAG, "Message for Sat Radio Received");
                    ESP_LOG_BUFFER_HEXDUMP(TAG, message.body, message.body_len, ESP_LOG_DEBUG);
                    sdrs_emulator(message);
                    break;

                // case CDC:
                //     ESP_LOGD(TAG, "Message for CD Changer Received");
                //     ESP_LOG_BUFFER_HEXDUMP(TAG, message.body, message.body_len, ESP_LOG_DEBUG);
                //     cdc_emulator(message);
                //     break;

                // case TEL:
                //     ESP_LOGD(TAG, "Message for TEL module Received");
                //     ESP_LOG_BUFFER_HEXDUMP(TAG, message.body, message.body_len, ESP_LOG_DEBUG);
                //     tel_emulator(message);
                //     break;

                default:
                    break;
            }
        } else {
            xQueueReset(kbus_rx_queue); // flush queue
        }
    }
}

static void cdc_emulator(kbus_message_t rx_msg) {
    //* CD Changer Messages from http://web.archive.org/web/20110320053244/http://ibus.stuge.se/CD_Changer
    kbus_message_t tx_msg = {   // CDC -> SORUCE
        .src = CDC,
        .dst = rx_msg.src,      // Address to sender of received msg
    };

    switch(rx_msg.body[0]) {
        case DEV_STAT_REQ:
            ESP_LOGD(TAG, "CDC Received: DEVICE STATUS REQUEST");
            send_dev_ready(CDC, rx_msg.src, false); // "Device Status Request" response "Device Status Ready"
            ESP_LOGD(TAG, "CDC Queued: DEVICE STATUS READY");
            return;

        case CD_CTRL_REQ: // TODO: React to different requests and reply appropriately
            ESP_LOGD(TAG, "CDC Received: CD CONTROL REQUEST");
            tx_msg.body[0] = CD_STAT_RPLY;
            tx_msg.body[1] = 0x00;  // STOP
            tx_msg.body[2] = 0x00;  // PAUSE requested on 0x02
            tx_msg.body[3] = 0x00;  // ERRORS byte, can || multiple flags
            tx_msg.body[4] = 0x21;  // DISCS loaded; each bit is a CD. 0x21 --> Discs 1 & 6
            tx_msg.body[5] = 0x00;  // ¯\_(ツ)_/¯ Padding?...
            tx_msg.body[6] = 0x01;  // DISC number in reader. 0x01 --> Disc 1
            tx_msg.body[7] = 0x01;  // TRACK number.
            tx_msg.body_len = 8;
            xQueueSend(kbus_tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
            ESP_LOGD(TAG, "CDC Queued: CD STATUS REPLY");
            return;

        default:
            ESP_LOGD(TAG, "CDC Received Other Command:");
            ESP_LOG_BUFFER_HEXDUMP(TAG, rx_msg.body, rx_msg.body_len, ESP_LOG_DEBUG);
            break;
    }
}

static void sdrs_emulator(kbus_message_t rx_msg) {
    kbus_message_t tx_msg = {   // SDRS -> SORUCE
        .src = SDRS,
        .dst = rx_msg.src,      // Address to sender of received msg
    };

    uint8_t cur_channel = 0x00, cur_bank = 0x00, cur_preset = 0x00;

    // static inline 

    switch(rx_msg.body[0]) {
        case DEV_STAT_REQ:
            ESP_LOGD(TAG, "SDRS Received: DEVICE STATUS REQUEST");
            send_dev_ready(SDRS, rx_msg.src, false); // "Device Status Request" response "Device Status Ready"
            ESP_LOGD(TAG, "SDRS Queued: DEVICE STATUS READY");
            return;~
        
        case SDRS_CTRL_REQ: {

            switch(rx_msg.body[1]) {

                case 0x00:  //? Bootup command?
                    ESP_LOGI(TAG, "SRDS Power On command received");
                    break;
                /**
                 * ? Might indeed be a power/mode update command like documented at:
                 * ? https://github.com/blalor/iPod_IBus_adapter/blob/f828d9327810512daa1dab1f9b7bb13dd9f80c21/doc/logs/log_analysis.txt#L9
                 * 
                 * ? Looks like it either confirms the SAT tuning on deactivatiohn, or this might be a
                 * ? brief status update after SAT is no longer the source. Analyzing the logs, the two different
                 * ? <3D 01 00> command messages recieved have matching channel && preset values as the regular
                 * ? status update messages.
                 * 
                 * ? Type              Status Update            Sleep Status
                 * ? Command             <3D 02 00>      <=>     <3D 01 00>
                 * ? Response Body   3E 02 00 95 20 04   <=>   3E 00 00 95 20 04
                 * !                          95 20 04                  95 20 04
                 * !                     channel 149, preset bank 2, preset num 0
                 * ?
                 */
                case 0x01:
                    tx_msg.body[0] = SDRS_STAT_RPLY;
                    tx_msg.body[1] = 0x00;  // Command 0x00
                    tx_msg.body[2] = 0x00;  // Padding or flags?
                    tx_msg.body[3] = 0x01;  // Current Channel
                    tx_msg.body[4] = 0x11;  // Preset Bank && Preset number (each gets a nibble)
                    tx_msg.body[5] = 0x04;  // ¯\_(ツ)_/¯   Some flag set on bit 2
                    tx_msg.body_len = 6;
                    xQueueSend(kbus_tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                   return;

                case 0x02:  // Status Update Req. ("NOW" message)
                case 0x03:  // Channel up
                case 0x08:  // Preset recall to preset in rx_msg.body[2]
                case 0x15:  // SAT pushed, change preset bank
                    tx_msg.body[0] = SDRS_STAT_RPLY;
                    tx_msg.body[1] = 0x02;  // Status Update
                    tx_msg.body[2] = 0x00;  // Padding or flags?
                    tx_msg.body[3] = 0x01;  // Current Channel
                    tx_msg.body[4] = 0x11;  // Preset Bank && Preset number (each gets a nibble)
                    tx_msg.body[5] = 0x04;  // ¯\_(ツ)_/¯   Some flag set on bit 2
                    tx_msg.body_len = 14;

                    memcpy(&tx_msg.body[6], "Station ", 9); // Channel Name Text
                    xQueueSend(kbus_tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                    return;

                case 0x0E:  // Artist Text Req.
                    tx_msg.body[0] = SDRS_STAT_RPLY;
                    tx_msg.body[1] = 0x01;  // Text Update
                    tx_msg.body[2] = 0x06;  // Padding or flags? Artist flag?
                    tx_msg.body[3] = 0x01;  // Current Channel
                    tx_msg.body[4] = 0x01;  // Bank 0 && Preset 1 for Artist Text
                    tx_msg.body[5] = 0x01;  // ¯\_(ツ)_/¯   Some flag set on bit 0
                    tx_msg.body_len = 13;

                    memcpy(&tx_msg.body[6], "Artist ", 8); // Artist Name Text
                    xQueueSend(kbus_tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                    return;

                case 0x0F:  // Song Text Req.
                    tx_msg.body[0] = SDRS_STAT_RPLY;
                    tx_msg.body[1] = 0x01;  // Text Update
                    tx_msg.body[2] = 0x07;  // Padding or flags? Song flag?
                    tx_msg.body[3] = 0x01;  // Channel
                    tx_msg.body[4] = 0x01;  // Bank 0 && Preset 1 for Song Text
                    tx_msg.body[5] = 0x01;  // ¯\_(ツ)_/¯   Some flag set on bit 0
                    tx_msg.body_len = 11;

                    memcpy(&tx_msg.body[6], "Song ", 6); // Song Name Text
                    xQueueSend(kbus_tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                    return;

                default:
                    break;
            }
        }

        default:
            break;
    }
}

static void tel_emulator(kbus_message_t rx_msg) {
    switch(rx_msg.body[0]) {
        case DEV_STAT_REQ:
            ESP_LOGD(TAG, "TEL Received: DEVICE STATUS REQUEST");
            send_dev_ready(TEL, rx_msg.src, false);
            ESP_LOGD(TAG, "TEL Queued: DEVICE STATUS READY");
            return;

        default:
            ESP_LOGD(TAG, "TEL Received Other Command:");
            ESP_LOG_BUFFER_HEXDUMP(TAG, rx_msg.body, rx_msg.body_len, ESP_LOG_DEBUG);
            break;
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
            ESP_LOGD(TAG, "MFL -> RAD/TEL Button Event");
            
            if(last_mfl_cmd){
                ESP_LOGD(TAG, "last_mfl_cmd: 0x%02x 0x%02x", last_mfl_cmd[0], last_mfl_cmd[1]);
            } else {
                ESP_LOGD(TAG, "last_mfl_cmd: NULL");
            }
            malloc_last_mfl();

            switch(mfl_cmd[1]) {
                //* A button down event received, let's store it.
                case 0x01:      // "search up pressed"
                case 0x08:      // "search down pressed"
                case 0x80: {    // "Send/End pressed"
                    ESP_LOGD(TAG, "MFL short press");
                    last_mfl_cmd[0] = mfl_cmd[0];
                    last_mfl_cmd[1] = mfl_cmd[1];
                    return; // Return, don't need to take any further action other than storing opening event.
                }

                //* A long press event
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
                case 0x90: // "SEND/END pressed long"
                    ESP_LOGD(TAG, "MFL R/T long press");
                    if(last_mfl_cmd[1] == mfl_cmd[1]){
                        return; // Return, this is a repeat long press event.
                    }
                    last_mfl_cmd[0] = mfl_cmd[0];
                    last_mfl_cmd[1] = mfl_cmd[1];
                    bt_command = AVRCP_PLAY;
                    break;

                //* A button up event, let's check previous state.
                case 0x21: // "search up released"
                case 0x28: // "search down released"
                case 0xA0: // "SEND/END released"
                    ESP_LOGD(TAG, "MFL arrow button released");
                    if(last_mfl_cmd[0] == mfl_cmd[0]){
                        ESP_LOGD(TAG, "Last BT command matches");
                        switch(last_mfl_cmd[1]) {
                            case 0x01:
                                bt_command = AVRCP_NEXT;
                                break;
                            case 0x11:
                                bt_command = AVRCP_FF_STOP;
                                break;
                            case 0x80:
                                bt_command = AVRCP_STOP;
                                break;
                            case 0x90:
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
                    ESP_LOGD(TAG, "Other MFL -> RAD/TEL Button Event: 0x%02x", mfl_cmd[1]);
                    // free_last_mfl(); // Command we don't care about, free last_mfl_cmd
                    break;
            }
            break;
        default:
            ESP_LOGD(TAG, "Other MFL Button Event: 0x%02x 0x%02x", mfl_cmd[0], mfl_cmd[1]);
            // free_last_mfl(); // last_mfl_cmd should't have been allocated, but try to free anyway
            break;
    }

    if(bt_command != BT_CMD_NOOP) { // Only put command on queue if it's a valid one
        ESP_LOGD(TAG, "Sending BT Command 0x%02x", bt_command);
        xQueueSend(bt_cmd_queue, &bt_command, (portTickType)portMAX_DELAY);
    }
}

static void display_message(uint8_t cmd, uint8_t layout, uint8_t flags, char* text){
    kbus_message_t message = {
        .src = RAD,
        .dst = LOC,
        .body = {cmd, layout, flags},
        .body_len = 18
    };

    memcpy(&message.body[3], text, message.body_len - 1);
    xQueueSend(kbus_tx_queue, &message, (portTickType)portMAX_DELAY);
}

#ifdef QUEUE_DEBUG
static void kbus_queue_watcher(){
    #define WATCHER_DELAY 10

    uint8_t kb_rx = 0, kb_tx = 0, bt_tx = 0;
    vTaskDelay(SECONDS(WATCHER_DELAY));

    while(1){
        kb_rx = uxQueueMessagesWaiting(kbus_rx_queue);
        kb_tx = uxQueueMessagesWaiting(kbus_tx_queue);
        bt_tx = uxQueueMessagesWaiting(bt_cmd_queue);

        printf("\n%sK-Bus Service Queued Messages%s\n", "\033[1m\033[4m\033[46m\033[K", LOG_RESET_COLOR);
        printf("kbus-rx\t%d\n", kb_rx);
        printf("kbus-tx\t%d\n", kb_tx);
        printf("bt-tx\t%d\n", bt_tx);

        vTaskDelay(SECONDS(WATCHER_DELAY));
    }
}

static void create_kbus_queue_watcher(){
    int task_ret = xTaskCreate(kbus_queue_watcher, "kbus_queue_watcher", 4096, NULL, 5, NULL);
    if(task_ret != pdPASS){ESP_LOGE(TAG, "kbus_queue_watcher creation failed with: %d", task_ret);}
}
#endif