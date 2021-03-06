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
#include "bt_common.h"
#include "sdrs_emulator.h"

// ! Debug Flags
// #define QUEUE_DEBUG

#define HERTZ(hz) ((1000/hz)/portTICK_RATE_MS)
#define SECONDS(sec) ((sec*1000) / portTICK_RATE_MS)
#define KBUS_TASK_PRIORITY configMAX_PRIORITIES-5

static const char* TAG = "kbus_service";
static QueueHandle_t bt_cmd_queue;
static QueueHandle_t bt_info_queue;
static QueueHandle_t kbus_rx_queue; //TODO: Message Buffer instead of Queue... lol, not available on esp-idf 4.0.2; will be on 4.3
static QueueHandle_t kbus_tx_queue; //TODO: See https://github.com/espressif/esp-idf/issues/4945 for details

static TaskHandle_t tel_display_tsk = NULL;

static sdrs_display_buf_t* sdrs_display_buf = NULL;

static void init_emulated_devs();
static void kbus_rx_task();
static void cdc_emulator(kbus_message_t rx_msg);
static void tel_emulator(kbus_message_t rx_msg);
static void mfl_handler(uint8_t mfl_cmd[2]);
static void display_tel_msg(uint8_t cmd, uint8_t layout, uint8_t flags, char* text);
static void bt_info_task();
static void tel_display_task();

#ifdef QUEUE_DEBUG
static void create_kbus_queue_watcher();
#endif

void init_kbus_service(QueueHandle_t bt_command_q, QueueHandle_t bt_track_info_q) {
    bt_cmd_queue = bt_command_q;
    bt_info_queue = bt_track_info_q;
    kbus_rx_queue = xQueueCreate(8, sizeof(kbus_message_t));
    kbus_tx_queue = xQueueCreate(4, sizeof(kbus_message_t));

    int tsk_ret = xTaskCreatePinnedToCore(kbus_rx_task, "kbus_rx", 4096, NULL, KBUS_TASK_PRIORITY, NULL, 1);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "kbus_rx creation failed with: %d", tsk_ret);}
    init_kbus_uart_driver(kbus_rx_queue, kbus_tx_queue);

    tsk_ret = xTaskCreate(init_emulated_devs, "emus_init", 4096, NULL, KBUS_TASK_PRIORITY+1, NULL);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "emus_init creation failed with: %d", tsk_ret);}

    tsk_ret = xTaskCreate(bt_info_task, "bt_trk_info", 4096, NULL, KBUS_TASK_PRIORITY-2, NULL);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "bt_trk_info creation failed with: %d", tsk_ret);}

    tsk_ret = xTaskCreate(tel_display_task, "tel_dis_tsk", 4096, NULL, KBUS_TASK_PRIORITY-2, &tel_display_tsk);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "tel_dis_tsk creation failed with: %d", tsk_ret);}

#ifdef QUEUE_DEBUG
    create_kbus_queue_watcher();
#endif
}

static void init_emulated_devs() {
    vTaskDelay(SECONDS(1));

    sdrs_display_buf = (sdrs_display_buf_t*) malloc(sizeof(sdrs_display_buf_t));
    sdrs_init_emulation(kbus_tx_queue, sdrs_display_buf);

    vTaskDelay(50);
    send_dev_ready(TEL, LOC, true);

    // vTaskDelay(50);
    // send_dev_ready(CDC, LOC, true);

    vTaskDelete(NULL);
}

static void bt_info_task() {
    bt_now_playing_info_t info;
    while(1) {
        if(xQueueReceive(bt_info_queue, (void *)&info, (portTickType)portMAX_DELAY)) {
            strcpy(sdrs_display_buf->chan_disp, "Spotify");
            strcpy(sdrs_display_buf->artist_disp, info.artist_name);
            strcpy(sdrs_display_buf->song_disp, info.track_title);

            // If incoming string is new, update MID
            if(!strcmp(sdrs_display_buf->song_disp, info.track_title)){
                if(tel_display_tsk != NULL) xTaskNotify(tel_display_tsk, 0x01, eSetBits);
            }
        }
        vTaskDelay(HERTZ(1)); // Rate limit updates to 1Hz
    }
    vTaskDelete(NULL); // In case we leave the loop, to avoid a panic
}

void send_dev_ready(uint8_t source, uint8_t dest, bool startup) {
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
                        // TODO: Need to guarantee it's only emitted once before requesting media begin playing
                    //     // Send AVRCP_PLAY command when ignition_status bits set to: Pos1_Acc Pos2_On
                    //     bt_cmd_type_t bt_command = AVRCP_PLAY;
                    //     xQueueSend(bt_cmd_queue, &bt_command, (portTickType)portMAX_DELAY);
                    }
                    // Only care for this one particular GLOBAL message right now
                    break;

                case SDRS:
                    ESP_LOGD(TAG, "Message for Sat Radio Received");
                    ESP_LOG_BUFFER_HEXDUMP(TAG, message.body, message.body_len, ESP_LOG_DEBUG);
                    sdrs_enqueue_msg(&message, 50);
                    break;

                // case CDC:
                //     ESP_LOGD(TAG, "Message for CD Changer Received");
                //     ESP_LOG_BUFFER_HEXDUMP(TAG, message.body, message.body_len, ESP_LOG_DEBUG);
                //     cdc_emulator(message);
                //     break;

                case TEL:
                    ESP_LOGD(TAG, "Message for TEL module Received");
                    ESP_LOG_BUFFER_HEXDUMP(TAG, message.body, message.body_len, ESP_LOG_DEBUG);
                    tel_emulator(message);
                    break;

                default:
                    break;
            }
        } else {
            xQueueReset(kbus_rx_queue); // flush queue
        }
    }
    vTaskDelete(NULL); // In case we leave the loop, to avoid a panic
}

static void cdc_emulator(kbus_message_t rx_msg) {   //? Own component like SDRS?...
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

static void tel_display_task() {
    char msg_buf[256];
    char mid_buf[12];
    
    static uint32_t notification;
    static const uint8_t text_limit = 11;
    static const uint8_t step_size = 8;
    
    uint16_t seconds = 0;
    uint8_t msg_len = 0;
    uint8_t msg_pos = 0;

    bool should_scroll = false;
    bool should_update = false;
    bool should_display = false;

    while(1){
        xTaskNotifyWait(0x00000000, 0x00000001, &notification, SECONDS(1)); // CLear 0x01 on exit and continue
        if(notification == 0x01) {
            should_display = true;
            should_scroll = false;
            seconds = 0;
            msg_pos = 0;
            bzero(mid_buf, sizeof(mid_buf));
            bzero(msg_buf, sizeof(msg_buf));

            sprintf(msg_buf, "%s<>%s", sdrs_display_buf->song_disp, sdrs_display_buf->artist_disp);
            msg_len = strlen(msg_buf);

            ESP_LOGI(TAG, "%s", msg_buf);

            if(msg_len > text_limit){
                should_scroll = true;
            }
        }
        
        should_update = !(seconds % 15);
        seconds++;

        if(should_display && should_update){

            if(should_scroll) {
                if(msg_pos + step_size > msg_len) msg_pos = 0; //Reset to start of buffer
                strncpy(mid_buf, msg_buf + msg_pos, text_limit); //strncpy pads with 0 if we go beyond \0 delimeter
                msg_pos+=step_size;
            
                ESP_LOGI(TAG, "Scrolling|| %s ||", mid_buf);
                display_tel_msg(UPDATE_MID, 0x42, 0x32, mid_buf);

            } else {
                ESP_LOGI(TAG, "Static|| %s ||", msg_buf);
                display_tel_msg(UPDATE_MID, 0x42, 0x32, msg_buf);
            }
        }
    }
}

static void display_tel_msg(uint8_t cmd, uint8_t layout, uint8_t flags, char* text) {
    uint8_t text_len = strlen(text);

    kbus_message_t message = {
        .src = TEL,
        .dst = IKE,
        .body = {cmd, layout, flags},
        .body_len = 3 + text_len
    };

    message.body_len = 3 + text_len;
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
