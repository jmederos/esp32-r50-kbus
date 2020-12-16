#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"

#include "kbus_uart_driver.h"

#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_16)
#define SERVICE_UART UART_NUM_2
#define LED_PIN (GPIO_NUM_2)

#define HERTZ(hz) ((1000/hz) / portTICK_RATE_MS)

//TODO: Add to kconfig
#define KBUS_TX_TEST_IS_ENABLED flase

static const int RX_BUF_SIZE = 1024;
static const int TX_BUF_SIZE = 512;
static const char* TAG = "kbus_driver";

static QueueHandle_t kbus_rx_queue;
static QueueHandle_t uart_rx_queue;

static void rx_task();

void init_kbus_uart_driver(QueueHandle_t kbus_queue) {
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

    ESP_LOGI(TAG, "Creating kbus_uart_driver rx task");
    xTaskCreatePinnedToCore(rx_task, "uart_rx_task", RX_BUF_SIZE*4, NULL, configMAX_PRIORITIES, NULL, 1);

    // Setup onboard LED
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    // Store kbus message queue
    kbus_rx_queue = kbus_queue;
}

int kbus_send_str(const char* logName, const char* str) {
    ESP_LOGV(logName, "Writing string to kbus");
    const int len = strlen(str); //! /0 delimited char array.
    const int txBytes = uart_write_bytes(SERVICE_UART, str, len);
    ESP_LOGD(logName, "Wrote %02x bytes to kbus", txBytes);
    return txBytes;
}

int kbus_send_bytes(const char* logName, const char* bytes, uint8_t numBytes) {
    ESP_LOGV(logName, "Writing %02x bytes to kbus", numBytes);
    const int txBytes = uart_write_bytes(SERVICE_UART, bytes, numBytes);
    ESP_LOGD(logName, "Wrote %02x bytes to kbus", txBytes);
    return txBytes;
}

/**
 * @brief Task that handles incoming kbus data
 */
static void rx_task() {
    static const char *RX_TASK_TAG = "RX_TASK";

    uart_event_t event;
    kbus_message_t rx_message;
    uint8_t msg_header[2] = {0,0};
    uint8_t* msg_buf = (uint8_t*) malloc(257); // Size of max possible kbus message
    
    while(1) {
        if(xQueueReceive(uart_rx_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            bzero(msg_buf, 257);
            bzero(rx_message.body, sizeof(rx_message.body));

            switch(event.type) {
                case UART_DATA:
                    gpio_set_level(LED_PIN, 1); // Turn on module LED

                    uart_read_bytes(SERVICE_UART, msg_header, 2, 0);           // Read header: src_addr + msg_len
                    uart_read_bytes(SERVICE_UART, msg_buf, msg_header[1], 0);   // Only read the bytes the message says to read

                    rx_message.src = msg_header[0];
                    rx_message.msg_len = msg_header[1];
                    rx_message.body_len = msg_header[1] - 2;                    // Body length for later iterating
                    rx_message.dst = msg_buf[0];                                // Copy message dst address to struct
                    memcpy(rx_message.body, msg_buf + 1, msg_header[1] - 1);    // Copy message body to struct using byte offsets
                    rx_message.chksum = msg_buf[msg_header[1] - 1];             // Copy message checksum to struct

                    ESP_LOGD(RX_TASK_TAG, "Read %d bytes from UART", event.size);
                    ESP_LOGD(RX_TASK_TAG, "KBUS\t0x%02x----->0x%02x\tLength: %d\tCHK:0x%02x", rx_message.src, rx_message.dst, rx_message.body_len+2, rx_message.chksum);
                    ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, rx_message.body, event.size-4, ESP_LOG_DEBUG);

                    xQueueSend(kbus_rx_queue, &rx_message, 1600);

                    gpio_set_level(LED_PIN, 0);
                    break;
                case UART_PARITY_ERR:
                case UART_FRAME_ERR:
                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    ESP_LOGE(RX_TASK_TAG, "UART Error: %d", event.type);
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
