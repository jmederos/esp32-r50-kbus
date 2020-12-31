// C stdlib includes
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// esp-idf includes
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"

// component includes
#include "kbus_uart_driver.h"

#define QUEUE_DEBUG

/**
 ** Pin numbers form Sparkfun ESP32 MicroMod Schematic
 ** https://cdn.sparkfun.com/assets/2/2/5/9/5/MicroMod_ESP32_Schematic.pdf
 */
#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_16)
#define ENABLE_PIN (GPIO_NUM_14)
#define SERVICE_UART UART_NUM_2
#define LED_PIN (GPIO_NUM_2)

#define HERTZ(hz) ((1000/hz)/portTICK_RATE_MS)
#define SECONDS(sec) ((sec*1000) / portTICK_RATE_MS)

#define TX_TASK_PRIORITY configMAX_PRIORITIES-1
#define RX_TASK_PRIORITY TX_TASK_PRIORITY-10

static const int RX_BUF_SIZE = 2048;
static const int TX_BUF_SIZE = 1024;
static const char* TAG = "kbus_driver";

static QueueHandle_t kbus_rx_queue;
static QueueHandle_t kbus_tx_queue;
static QueueHandle_t uart_rx_queue;

static int kbus_send_bytes(const char* logName, const char* bytes, uint8_t numBytes);
static void rx_task();
static void tx_task();
static uint8_t calc_checksum();

#ifdef QUEUE_DEBUG
static void create_uart_queue_watcher();
#endif


void init_kbus_uart_driver(QueueHandle_t rx_queue, QueueHandle_t tx_queue) {
    // Store kbus message queues
    kbus_rx_queue = rx_queue;
    kbus_tx_queue = tx_queue;

    //* Configuring based on i/k bus spec:
    //* http://web.archive.org/web/20070513012128/http://www.openbmw.org/bus/
    ESP_LOGI(TAG, "Initializing Kbus UART");
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    ESP_ERROR_CHECK(uart_param_config(SERVICE_UART, &uart_config));

    // UART2 is supposed to be on 16/17 by default; didn't seem to be the case when testing...
    ESP_ERROR_CHECK(uart_set_pin(SERVICE_UART, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // uart_set_pin() sets GPIO_PULLUP_ONLY on RX, not TX. Need it to avoid taking control of k-bus when only listening.
    ESP_ERROR_CHECK(gpio_set_pull_mode(TXD_PIN, GPIO_PULLUP_ONLY));
    // We won't use a buffer for sending data.
    ESP_ERROR_CHECK(uart_driver_install(SERVICE_UART, RX_BUF_SIZE, TX_BUF_SIZE, 32, &uart_rx_queue, 0));
    // Make sure we're in standard uart mode
    ESP_ERROR_CHECK(uart_set_mode(SERVICE_UART, UART_MODE_UART));

    //* Doesn't work as expected **********************************
    // Write some data to tx line so it's not in a dominant state
    //// char kbus_init_byte = 0x00;
    //// kbus_send_bytes("kbus-tx-init", &kbus_init_byte, 1);
    // ************************************************************

    // Create RX and TX task loops
    ESP_LOGI(TAG, "Creating kbus_uart_driver rx task");
    xTaskCreatePinnedToCore(rx_task, "uart_rx", RX_BUF_SIZE*4, NULL, RX_TASK_PRIORITY, NULL, 1);
    ESP_LOGI(TAG, "Creating kbus_uart_driver tx task");
    xTaskCreatePinnedToCore(tx_task, "uart_tx", TX_BUF_SIZE*4, NULL, TX_TASK_PRIORITY, NULL, 1);

    // Setup onboard LED
    ESP_ERROR_CHECK(gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 0));

    // Pull enable pin high so we can listen to all k-bus traffic
    ESP_ERROR_CHECK(gpio_set_pull_mode(ENABLE_PIN, GPIO_PULLUP_ONLY));

#ifdef QUEUE_DEBUG
    create_uart_queue_watcher();
#endif
}

static int kbus_send_bytes(const char* logName, const char* bytes, uint8_t numBytes) {
    ESP_LOGV(logName, "Writing %02x bytes to kbus", numBytes);
    const int txBytes = uart_write_bytes(SERVICE_UART, bytes, numBytes);
    uart_wait_tx_done(SERVICE_UART, (portTickType)portMAX_DELAY); // Block until all bytes are sent
    ESP_LOGD(logName, "Wrote %02x bytes to kbus", txBytes);
    return txBytes;
}

/**
 * @brief Task that handles incoming kbus data
 */
static void rx_task() {
    static const char *RX_TASK_TAG = "uart_rx";

    uart_event_t event;
    kbus_message_t rx_message;
    uint8_t rx_checksum = 0;
    uint8_t msg_header[2] = {0,0};
    uint8_t* msg_buf = (uint8_t*) malloc(257); // Size of max possible kbus message
    
    while(1) {
        if(xQueueReceive(uart_rx_queue, (void * )&event,  (portTickType)portMAX_DELAY)) {
            rx_checksum = 0;
            bzero(msg_buf, 257);
            bzero(rx_message.body, sizeof(rx_message.body));

            switch(event.type) {
                case UART_DATA:
                    gpio_set_level(LED_PIN, 1); // Turn on module LED

                    uart_read_bytes(SERVICE_UART, msg_header, 2, 0);            // Read header: src_addr + msg_len
                    uart_read_bytes(SERVICE_UART, msg_buf, msg_header[1], 0);   // Only read the bytes the message says to read
                    ESP_LOGD(RX_TASK_TAG, "Read %d bytes from UART", event.size);

                    rx_message.src = msg_header[0];
                    rx_message.body_len = msg_header[1] - 2;                    // Only store body length for easier iterating
                    rx_message.dst = msg_buf[0];                                // Copy message dst address to struct
                    memcpy(rx_message.body, msg_buf + 1, msg_header[1] - 1);    // Copy message body to struct using byte offsets
                    rx_checksum = msg_buf[msg_header[1] - 1];                   // Copy message checksum to struct

                    if(rx_checksum == calc_checksum(&rx_message)){
                        ESP_LOGD(RX_TASK_TAG, "KBUS\t0x%02x --> 0x%02x\tLength: %d\tCHK:0x%02x", rx_message.src, rx_message.dst, rx_message.body_len+2, rx_checksum);
                        xQueueSend(kbus_rx_queue, &rx_message, 10);
                    } else {
                        bzero(msg_buf, 257);        // flush buffer
                        ESP_LOGW(RX_TASK_TAG, "Invalid message received!");
                    }
                    ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, rx_message.body, event.size-4, ESP_LOG_DEBUG);

                    gpio_set_level(LED_PIN, 0);
                    break;

                case UART_PARITY_ERR:
                    ESP_LOGW(RX_TASK_TAG, "UART_PARITY_ERR");
                    break;
                case UART_FRAME_ERR:
                    ESP_LOGW(RX_TASK_TAG, "UART_FRAME_ERR");
                    break;
                case UART_BUFFER_FULL:  // Error 0x02
                case UART_FIFO_OVF:     // Error 0x03
                    ESP_LOGE(RX_TASK_TAG, "UART Error: %d", event.type);
                    bzero(msg_buf, 257);        // flush buffer
                    xQueueReset(uart_rx_queue); // flush queue
                    break;

                default:
                    ESP_LOGI(RX_TASK_TAG, "Other UART Event: %d", event.type);
                    break;
            }
        }
    }
    free(msg_buf);
    msg_buf=NULL;
}

static void tx_task() {
    static const char *TX_TASK_TAG = "uart_tx";

    kbus_message_t tx_message;
    char* tx_buf = (char*) malloc(257);
    int bytes_sent = 0;
    uint8_t msg_len = 0;
    uint8_t total_tx_bytes = 0;
    
    while(1) {
        bytes_sent = 0;
        msg_len = 0;
        total_tx_bytes = 0;
        bzero(tx_buf, 257);

        if(xQueueReceive(kbus_tx_queue, (void * )&tx_message, (portTickType)portMAX_DELAY)) {
            ESP_LOGD(TX_TASK_TAG, "tx_message: 0x%02x --> 0x%02x", tx_message.src, tx_message.dst);
            ESP_LOG_BUFFER_HEXDUMP(TX_TASK_TAG, tx_message.body, tx_message.body_len, ESP_LOG_VERBOSE);

            msg_len = tx_message.body_len + 2;  // Derive message length from body length
            total_tx_bytes = msg_len + 2;       // Total bytes to be put on the wire

            tx_buf[0] = (char) tx_message.src;
            tx_buf[1] = (char) msg_len;
            tx_buf[2] = (char) tx_message.dst;
            tx_buf[3 + tx_message.body_len] = (char) calc_checksum(&tx_message);    // Calculate message checksum

            memcpy(&tx_buf[3], tx_message.body, tx_message.body_len);               // Copy message body to buffer
            ESP_LOG_BUFFER_HEXDUMP(TX_TASK_TAG, tx_buf, total_tx_bytes, ESP_LOG_DEBUG);

            bytes_sent = kbus_send_bytes(TX_TASK_TAG, tx_buf, total_tx_bytes);      // Put bytes on the wire

            if(total_tx_bytes == bytes_sent){
                ESP_LOGD(TX_TASK_TAG, "Successfully sent %d/%d bytes", bytes_sent, total_tx_bytes);
            } else {
                ESP_LOGW(TX_TASK_TAG, "Only sent %d/%d bytes!", bytes_sent, total_tx_bytes);
            }
        } else {
            xQueueReset(kbus_tx_queue); // flush queue
        }
    }
}

static uint8_t calc_checksum(kbus_message_t* message) {
    uint8_t checksum = 0x00;
    checksum ^= message->src ^ message->dst ^ (message->body_len + 2);

    for(uint8_t i = 0; i < message->body_len; i++) {
        checksum ^= message->body[i];
    }
    return checksum;
}

#ifdef QUEUE_DEBUG
static void uart_queue_watcher(){
    #define WATCHER_DELAY 10

    uint8_t kb_rx = 0, kb_tx = 0, uart_rx = 0;
    int uart_fifo = 0;
    vTaskDelay(SECONDS(WATCHER_DELAY));

    while(1){
        kb_rx = uxQueueMessagesWaiting(kbus_rx_queue);
        kb_tx = uxQueueMessagesWaiting(kbus_tx_queue);
        uart_rx = uxQueueMessagesWaiting(uart_rx_queue);
        ESP_ERROR_CHECK(uart_get_buffered_data_len(SERVICE_UART, (size_t*)&uart_fifo));

        printf("\n%sUART Driver Queued Messages%s\n", "\033[1m\033[4m\033[43m\033[K", LOG_RESET_COLOR);
        printf("kbus-rx\t%d\n", kb_rx);
        printf("kbus-tx\t%d\n", kb_tx);
        printf("uart_rx\t%d\n", uart_rx);
        printf("uart_fi\t%d\n", uart_fifo);

        vTaskDelay(SECONDS(WATCHER_DELAY));
    }
}

static void create_uart_queue_watcher(){
    int task_ret = xTaskCreate(uart_queue_watcher, "uart_queue_watcher", 4092, NULL, 5, NULL);
    if(task_ret != pdPASS){ESP_LOGE(TAG, "uart_queue_watcher creation failed with: %d", task_ret);}
}
#endif