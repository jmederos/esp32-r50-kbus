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

// ! Debug Flags
// #define QUEUE_DEBUG

/**
 ** Pin numbers from Sparkfun ESP32 MicroMod Schematic
 ** https://cdn.sparkfun.com/assets/2/2/5/9/5/MicroMod_ESP32_Schematic.pdf
 */
#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_16)
#define ENABLE_PIN (GPIO_NUM_14)
#define DRIVER_UART UART_NUM_2
#define LED_PIN (GPIO_NUM_2)

#define HERTZ(hz) ((1000/hz)/portTICK_RATE_MS)
#define SECONDS(sec) ((sec*1000) / portTICK_RATE_MS)

#define TX_TASK_PRIORITY configMAX_PRIORITIES-1
#define RX_TASK_PRIORITY TX_TASK_PRIORITY-10

static const int RX_QUEUE_SIZE = 32;
static const int RX_BUF_SIZE = 2048;
static const int TX_BUF_SIZE = 512;
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

/**
 * @brief Initializes UART driver && queue handling functions
 * 
 * @param[in] rx_queue  Queue emitting recieved k-bus messages to service handling logic/device emulation
 * @param[in] tx_queue  Queue receiving k-bus messages from service handling logic/device emulation
 */
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
    ESP_ERROR_CHECK(uart_param_config(DRIVER_UART, &uart_config));

    // UART2 is supposed to be on 16/17 by default; didn't seem to be the case when testing...
    ESP_ERROR_CHECK(uart_set_pin(DRIVER_UART, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // uart_set_pin() sets GPIO_PULLUP_ONLY on RX, not TX. Need it to avoid taking control of k-bus when only listening.
    ESP_ERROR_CHECK(gpio_set_pull_mode(TXD_PIN, GPIO_PULLUP_ONLY));
    // Install driver on DRIVER_UART with RX_BUF_SIZE and TX_BUF_SIZE buffers.
    ESP_ERROR_CHECK(uart_driver_install(DRIVER_UART, RX_BUF_SIZE, TX_BUF_SIZE, RX_QUEUE_SIZE, &uart_rx_queue, 0));
    // Make sure we're in standard uart mode
    ESP_ERROR_CHECK(uart_set_mode(DRIVER_UART, UART_MODE_UART));

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

    /**********************************************************************************************************
     * TODO:  K-Bus Message Decoding task loop with streambuffer (when 4.3 becomes available)
     * //Create K-Bus Message Decoding task loop and streambuffer
     * ESP_LOGI(TAG, "Creating kbus_uart_driver decode task");
     * xTaskCreatePinnedToCore(kbus_decode_task, "kb_decode", RX_BUF_SIZE*2, NULL, TX_TASK_PRIORITY, NULL, 1);
     **********************************************************************************************************/

    // Setup onboard LED (Sparkfun ESP32 MicroMod)
    ESP_ERROR_CHECK(gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 0));

    // Pull transciever enable pin high so we can listen to all k-bus traffic. (TI SN65HVDA195QDRQ1)
    ESP_ERROR_CHECK(gpio_set_pull_mode(ENABLE_PIN, GPIO_PULLUP_ONLY));

#ifdef QUEUE_DEBUG
    create_uart_queue_watcher();
#endif
}

/**
 * @brief Task that handles outgoing messages to LIN transciever and writes to UART.
 * 
 * @param[in] logName   `char*` to string describing log tag
 * @param[in] bytes     `char*` to array of `bytes` to be written to UART
 * @param[in] numBytes  `uint8_t` number of bytes to be written to UART
 * 
 * @return `int` number of bytes `txBytes` written to UART
 */
static int kbus_send_bytes(const char* logName, const char* bytes, uint8_t numBytes) {
    ESP_LOGV(logName, "Writing %02x bytes to kbus", numBytes);
    const int txBytes = uart_write_bytes(DRIVER_UART, bytes, numBytes);
    uart_wait_tx_done(DRIVER_UART, (portTickType)portMAX_DELAY); // Block until all bytes are sent
    ESP_LOGD(logName, "Wrote %02x bytes to kbus", txBytes);
    return txBytes;
}

/**
 * @brief Decodes k-bus messages and sends to service queue.
 * 
 * @param[in] buffer        `uint8_t*` to UART rx data buffer
 * @param[in] event_size    `size_t` number of bytes to read from buffer
 * 
 * @return `uint8_t` number of messages sent to k-bus service.
 * 
 * @todo Setup as a task when streambuffers are available
 */
static uint8_t decode_and_send_buffer(uint8_t* buffer, size_t event_size) {
    uint8_t messages_sent = 0;

    size_t cur_byte = 0;
    kbus_message_t message;
    uint8_t msg_len = 0;
    uint8_t rx_checksum = 0;
    uint8_t cal_checksum = 0;

    if(event_size < 5) { //Don't bother decoding, less bytes than smallest valid kbus message.
        ESP_LOGW("decode_buf", "Less than 5 bytes in buffer!");
        return messages_sent;
    }

    do {
        message.src = buffer[cur_byte];     // Store message source address
        msg_len = buffer[cur_byte + 1];     // Keep msg_len locally for readability
        message.body_len = msg_len - 2;     // Only store body_len => passed message length - (dst_byte + chksum_byte)
        message.dst = buffer[cur_byte + 2]; // Store message dest address

        cur_byte += 3;                      // Advance pointer to start of message body
        memcpy(message.body, buffer + cur_byte, message.body_len); // Copy message body to struct

        cur_byte += (message.body_len);     // Advance pointer to checksum byte
        rx_checksum = buffer[cur_byte];     // Keep for comparision to calculated value

        cal_checksum = calc_checksum(&message);
        if(rx_checksum == cal_checksum){
            ESP_LOG_BUFFER_HEXDUMP("decode_buf", message.body, message.body_len, ESP_LOG_DEBUG);
            xQueueSend(kbus_rx_queue, &message, (portTickType)portMAX_DELAY);
            messages_sent++;
        } else {
            ESP_LOGD("decode_buf", "Checksum mismatch!");
            ESP_LOGD("decode_buf", "rx_chk: 0x%02x\tcal_chk: 0x%02x", rx_checksum, cal_checksum);
            ESP_LOGD("decode_buf", "0x%02x -> 0x%02x\t0x%02x bytes\t0x%02x", message.src, message.dst, msg_len, message.body[0]);
            ESP_LOG_BUFFER_HEXDUMP("decode_buf", buffer, event_size, ESP_LOG_DEBUG);
            ESP_LOGD("decode_buf", "cur_byte: %d\t event_size: %d", cur_byte, event_size);
        }
    } while(cur_byte < event_size);

    return messages_sent;
}

/**
 * @brief Task that handles incoming UART data from LIN transciever.
 */
static void rx_task() {
    static const char *RX_TASK_TAG = "uart_rx";
    uart_event_t event;
    uint8_t* msg_buf = (uint8_t*) malloc(RX_BUF_SIZE);
    uint8_t msgs_sent = 0;

    while(1) {
        if(xQueueReceive(uart_rx_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            bzero(msg_buf, RX_BUF_SIZE);

            switch(event.type) {
                case UART_DATA:
                    gpio_set_level(LED_PIN, 1); // Turn on module LED

                    uart_read_bytes(DRIVER_UART, msg_buf, event.size, 0);      // Ensure we read the whole event to avoid filling buffer
                    ESP_LOGD(RX_TASK_TAG, "Read %d bytes from UART", event.size);

                    msgs_sent = decode_and_send_buffer(msg_buf, event.size);
                    ESP_LOGD(RX_TASK_TAG, "Sent %d messages to kbus service", msgs_sent);

                    ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, msg_buf, event.size, ESP_LOG_DEBUG);

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
                    // Overflow and full buffer errors, ensure everything is cleared
                    bzero(msg_buf, RX_BUF_SIZE);    // clear buffer
                    xQueueReset(uart_rx_queue);     // reset queue
                    uart_flush(DRIVER_UART);        // flush UART ring buffer
                    break;

                default:
                    ESP_LOGI(RX_TASK_TAG, "Other UART Event: %d", event.type);
                    break;
            }
        } else {
            uart_flush(DRIVER_UART);   // flush UART ring buffer if nothing left to read from queue.
        }
    }
    free(msg_buf);
    msg_buf=NULL;
}

/**
 * @brief Task that handles outgoing messages to LIN transciever and writes to UART.
 */
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

            // Begin message encoding
            // TODO: Encoding function?
            msg_len = tx_message.body_len + 2;  // Derive message length from body length
            total_tx_bytes = msg_len + 2;       // Total bytes to be put on the wire

            tx_buf[0] = (char) tx_message.src;                                      // Set message source address
            tx_buf[1] = (char) msg_len;                                             // Set message length
            tx_buf[2] = (char) tx_message.dst;                                      // Set message destination
            tx_buf[3 + tx_message.body_len] = (char) calc_checksum(&tx_message);    // Calculate message checksum

            memcpy(&tx_buf[3], tx_message.body, tx_message.body_len);               // Copy message body to buffer
            ESP_LOG_BUFFER_HEXDUMP(TX_TASK_TAG, tx_buf, total_tx_bytes, ESP_LOG_DEBUG);
            // End message encoding

            bytes_sent = kbus_send_bytes(TX_TASK_TAG, tx_buf, total_tx_bytes);      // Put bytes on the wire

            if(total_tx_bytes == bytes_sent){
                ESP_LOGD(TX_TASK_TAG, "Successfully sent %d/%d bytes", bytes_sent, total_tx_bytes);
            } else {
                ESP_LOGW(TX_TASK_TAG, "Only sent %d/%d bytes!", bytes_sent, total_tx_bytes);
            }
        }
    }
}

/**
 * @brief Calculates K-Bus message XOR checksum and returns it.
 * 
 * @param[in] message K-Bus message for which to calculate checksum.
 * 
 * @return Calculated `checkshum` byte.
 */
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
        ESP_ERROR_CHECK(uart_get_buffered_data_len(DRIVER_UART, (size_t*)&uart_fifo));

        printf("\n%sUART Driver Queued Messages%s\n", "\033[1m\033[4m\033[43m\033[K", LOG_RESET_COLOR);
        printf("kbus-rx\t%d\n", kb_rx);
        printf("kbus-tx\t%d\n", kb_tx);
        printf("uart_rx\t%d\n", uart_rx);
        printf("uart_fi\t%d\n", uart_fifo);

        vTaskDelay(SECONDS(WATCHER_DELAY));
    }
}

static void create_uart_queue_watcher(){
    int task_ret = xTaskCreate(uart_queue_watcher, "uart_queue_watcher", 4096, NULL, 5, NULL);
    if(task_ret != pdPASS){ESP_LOGE(TAG, "uart_queue_watcher creation failed with: %d", task_ret);}
}
#endif