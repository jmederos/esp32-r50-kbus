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
#include "kbus_defines.h"
#include "kbus_service.h"
#include "sdrs_emulator.h"

#define HERTZ(hz) ((1000/hz)/portTICK_RATE_MS)
#define SECONDS(sec) ((sec*1000) / portTICK_RATE_MS)
#define EMU_TASK_PRIORITY configMAX_PRIORITIES-5

static const char* TAG = "sdrs_emu";
static QueueHandle_t rx_queue;
static QueueHandle_t tx_queue;

static uint8_t cur_channel = 0xaf, cur_bank = 0x00, cur_preset = 0x00;

static sdrs_display_buf_t* display_buf = NULL;

static inline uint8_t bank_preset_byte() { return (cur_bank << 4) | cur_preset; }

static void emu_task();

void sdrs_init_emulation(QueueHandle_t kbus_tx_queue, sdrs_display_buf_t* display_buffer){
    // Own queue for SDRS messages, don't want to have multiple readers on the main kbus rx queue
    rx_queue = xQueueCreate(4, sizeof(kbus_message_t));
    tx_queue = kbus_tx_queue;
    display_buf = display_buffer;

    sprintf(display_buf->chan_disp, "iPhone - No Info");
    sprintf(display_buf->artist_disp, "No Artist Info");
    sprintf(display_buf->song_disp, "No Song Info");
    sprintf(display_buf->esn_disp, "112358132134");

    int tsk_ret = xTaskCreatePinnedToCore(emu_task, "sdrs_emu", 4096, NULL, EMU_TASK_PRIORITY, NULL, 1);
    if(tsk_ret != pdPASS){ ESP_LOGE(TAG, "sdrs_emu creation failed with: %d", tsk_ret);}

    send_dev_ready(SDRS, LOC, true);
}

uint8_t sdrs_enqueue_msg(void* message, TickType_t ticks_to_wait) {
    return xQueueSend(rx_queue, message, ticks_to_wait);
}

static void emu_task() {
    kbus_message_t rx_msg;
    kbus_message_t tx_msg = {   
        .src = SDRS
    };

    while(1) {
        if(xQueueReceive(rx_queue, (void * )&rx_msg, (portTickType)portMAX_DELAY)) {
            tx_msg.dst = rx_msg.src;      // SDRS -> SOURCE

            switch(rx_msg.body[0]) {

                case DEV_STAT_REQ:
                    ESP_LOGD(TAG, "SDRS Received: DEVICE STATUS REQUEST");
                    send_dev_ready(SDRS, rx_msg.src, false); // "Device Status Request" response "Device Status Ready"
                    ESP_LOGD(TAG, "SDRS Queued: DEVICE STATUS READY");
                    break;
                
                case SDRS_CTRL_REQ: {
                    tx_msg.body[0] = SDRS_STAT_RPLY;

                    switch(rx_msg.body[1]) {
                        case SDRS_POWER_MODE:  //? Bootup command?
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
                        case SDRS_REQ_SLEEP:
                            tx_msg.body[1] = SDRS_POWER_MODE;  // Command 0x00
                            tx_msg.body[2] = 0x00;  // Padding or flags?
                            tx_msg.body[3] = cur_channel;  // Current Channel
                            tx_msg.body[4] = bank_preset_byte();  // Preset Bank && Preset number (each gets a nibble)
                            tx_msg.body[5] = 0x04;  // ¯\_(ツ)_/¯   Some flag set on bit 2
                            tx_msg.body_len = 6;
                            xQueueSend(tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                            break;

                        case SDRS_REQ_CHAN_UP:  // Channel up
                            cur_channel++;
                            //! Fall through to send packet
                        case SDRS_HEARTBEAT:  // Status Update Req. ("NOW" message)
                            tx_msg.body[1] = SDRS_HEARTBEAT;        // Status Update
                            tx_msg.body[2] = 0x00;                  // Padding or flags?
                            tx_msg.body[3] = cur_channel;           // Current Channel
                            tx_msg.body[4] = bank_preset_byte();    // Preset Bank && Preset number (each gets a nibble)
                            tx_msg.body[5] = 0x04;                  // ¯\_(ツ)_/¯   Some flag set on bit 2
                            tx_msg.body_len = 6;
                            xQueueSend(tx_queue, &tx_msg, (portTickType)portMAX_DELAY);

                            vTaskDelay(SECONDS(1));
                            tx_msg.body[1] = SDRS_UPDATE_TXT;       // Text Update
                            tx_msg.body[2] = 0x00;                  // Padding or flags?
                            tx_msg.body[3] = cur_channel;           // Current Channel
                            tx_msg.body[4] = bank_preset_byte();    // Preset Bank && Preset number (each gets a nibble)
                            tx_msg.body[5] = 0x04;                  // ¯\_(ツ)_/¯   Some flag set on bit 2
                            tx_msg.body_len = 6 + strlen(display_buf->chan_disp);

                            memcpy(&tx_msg.body[6], display_buf->chan_disp, strlen(display_buf->chan_disp)); // Channel Name Text
                            xQueueSend(tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                            break;

                        case SDRS_REQ_CHAN_DN:  // Channel down
                            cur_channel--;
                            tx_msg.body[1] = SDRS_CHAN_DN_ACK;      // Down ACK
                            tx_msg.body[2] = 0x00;                  // Padding or flags?
                            tx_msg.body[3] = cur_channel;           // Current Channel
                            tx_msg.body[4] = bank_preset_byte();    // Preset Bank && Preset number (each gets a nibble)
                            tx_msg.body[5] = 0x04;                  // ¯\_(ツ)_/¯   Some flag set on bit 2
                            tx_msg.body_len = 6;
                            xQueueSend(tx_queue, &tx_msg, (portTickType)portMAX_DELAY);

                            vTaskDelay(SECONDS(1));
                            tx_msg.body[1] = SDRS_UPDATE_TXT;       // Text Update
                            tx_msg.body[2] = 0x00;                  // Padding or flags?
                            tx_msg.body[3] = cur_channel;           // Current Channel
                            tx_msg.body[4] = bank_preset_byte();    // Preset Bank && Preset number (each gets a nibble)
                            tx_msg.body[5] = 0x04;                  // ¯\_(ツ)_/¯   Some flag set on bit 2
                            tx_msg.body_len = 6 + strlen(display_buf->chan_disp);
                            
                            memcpy(&tx_msg.body[6], display_buf->chan_disp, strlen(display_buf->chan_disp)); // Channel Name Text
                            xQueueSend(tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                            break;

                        case SDRS_REQ_PRESET:  // Preset recall to preset in rx_msg.body[2]
                            cur_preset = rx_msg.body[2];            // Let's just agree with the RAD
                            tx_msg.body[1] = SDRS_HEARTBEAT;        // Status Update
                            tx_msg.body[2] = 0x00;                  // Padding or flags?
                            tx_msg.body[3] = cur_channel;           // Current Channel
                            tx_msg.body[4] = bank_preset_byte();    // Preset Bank && Preset number (each gets a nibble)
                            tx_msg.body[5] = 0x04;                  // ¯\_(ツ)_/¯   Some flag set on bit 2
                            tx_msg.body_len = 6 + strlen(display_buf->chan_disp);

                            memcpy(&tx_msg.body[6], display_buf->chan_disp, strlen(display_buf->chan_disp)); // Channel Name Text
                            xQueueSend(tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                            break;

                        case SDRS_REQ_ESN:  // SAT long press, show ESN
                            tx_msg.body[1] = SDRS_UPDATE_TXT;       // Status Update
                            tx_msg.body[2] = 0x0c;                  // Padding or flags?
                            tx_msg.body[3] = 0x30;                  // Current Channel set to 48 (0x30)
                            tx_msg.body[4] = 0x30;                  // Presets byte is set to 0x30 for ESN
                            tx_msg.body[5] = 0x30;                  // ¯\_(ツ)_/¯ 
                            tx_msg.body_len = 6 + strlen(display_buf->esn_disp);

                            memcpy(&tx_msg.body[6], display_buf->esn_disp, strlen(display_buf->esn_disp)); // Channel Name Text
                            xQueueSend(tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                            break;

                        case SDRS_REQ_BANK_UP:  // SAT pushed, change preset bank
                            cur_bank++;
                            tx_msg.body[1] = SDRS_HEARTBEAT;        // Status Update
                            tx_msg.body[2] = 0x00;                  // Padding or flags?
                            tx_msg.body[3] = cur_channel;           // Current Channel
                            tx_msg.body[4] = bank_preset_byte();    // Preset Bank && Preset number (each gets a nibble)
                            tx_msg.body[5] = 0x04;                  // ¯\_(ツ)_/¯   Some flag set on bit 2
                            tx_msg.body_len = 6 + strlen(display_buf->chan_disp);

                            memcpy(&tx_msg.body[6], display_buf->chan_disp, strlen(display_buf->chan_disp)); // Channel Name Text
                            xQueueSend(tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                            break;

                        case SDRS_REQ_ARTIST:  // Artist Text Req.
                            tx_msg.body[1] = SDRS_UPDATE_TXT;       // Text Update
                            tx_msg.body[2] = 0x06;                  // Padding or flags? Artist flag?
                            tx_msg.body[3] = cur_channel;           // Current Channel
                            tx_msg.body[4] = 0x01;                  // Bank 0 && Preset 1 for Artist Text
                            tx_msg.body[5] = 0x01;                  // ¯\_(ツ)_/¯   Some flag set on bit 0
                            tx_msg.body_len = 6 + strlen(display_buf->artist_disp);

                            memcpy(&tx_msg.body[6], display_buf->artist_disp, strlen(display_buf->artist_disp)); // Artist Name Text
                            xQueueSend(tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                            break;

                        case SDRS_REQ_SONG:  // Song Text Req.
                            tx_msg.body[1] = SDRS_UPDATE_TXT;       // Text Update
                            tx_msg.body[2] = 0x07;                  // Padding or flags? Song flag?
                            tx_msg.body[3] = cur_channel;           // Channel
                            tx_msg.body[4] = 0x01;                  // Bank 0 && Preset 1 for Song Text
                            tx_msg.body[5] = 0x01;                  // ¯\_(ツ)_/¯   Some flag set on bit 0
                            tx_msg.body_len = 6 + strlen(display_buf->song_disp);

                            memcpy(&tx_msg.body[6], display_buf->song_disp, strlen(display_buf->song_disp)); // Song Name Text
                            xQueueSend(tx_queue, &tx_msg, (portTickType)portMAX_DELAY);
                            break;

                        default:
                            break;
                    }
                }
                default:
                    break;
            }
        }
    }
}
