#ifndef KBUS_UART_DRIVER_H
#define KBUS_UART_DRIVER_H

typedef struct {
    uint8_t body[253];

    uint8_t src;
    uint8_t body_len;
    uint8_t msg_len;
    uint8_t dst;
    uint8_t chksum;
}kbus_message_t; 

/**
 * @brief Initializes UART_2 pins, installs driver, creates separate tasks for tx and rx.
 * 
 * @note aborts if any of the uart calls fails.
 * 
 */
void init_kbus_uart_driver(QueueHandle_t kbus_queue);

// TODO: Refactor kbus sending w/queue instead of a call
/**
 * @brief Writes `numBytes` bytes to kbus UART.
 * 
 * @param logName
 * @param bytes
 * @param numBytes
 * 
 * @returns Number of bytes transmitted
 * 
 */
int kbus_send_bytes(const char* logName, const char* bytes, uint8_t numBytes);
#endif //KBUS_UART_DRIVER_H