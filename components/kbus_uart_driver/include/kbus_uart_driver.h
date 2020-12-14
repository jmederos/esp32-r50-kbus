#ifndef KBUS_UART_DRIVER_H
#define KBUS_UART_DRIVER_H

/**
 * @brief Initializes UART_2 pins, installs driver, creates separate tasks for tx and rx.
 * 
 * @note aborts if any of the uart calls fails.
 * 
 * TODO: graceful error handling.
 */
void init_kbus_uart_driver(void (*rx_callback)(uint8_t* data, uint8_t rx_bytes), uint8_t rx_poll_hz);

/**
 * @brief Writes string to kbus UART.
 * 
 * @param logName
 * @param data
 * 
 * @returns Number of bytes transmitted
 * 
 */
int kbus_send_str(const char* logName, const char* str);

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