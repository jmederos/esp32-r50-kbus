#ifndef KBUS_UART_DRIVER_H
#define KBUS_UART_DRIVER_H

typedef struct {
    uint8_t body[253];
    uint8_t body_len;
    uint8_t src;
    uint8_t dst;
}kbus_message_t; 

/**
 * @brief Initializes UART_2 pins, installs driver, creates separate tasks for tx and rx.
 * 
 * @note aborts if any of the uart calls fails.
 * 
 */
void init_kbus_uart_driver(QueueHandle_t rx_queue, QueueHandle_t tx_queue);
#endif //KBUS_UART_DRIVER_H
