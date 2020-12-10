#ifndef KBUS_UART_DRIVER_H
#define KBUS_UART_DRIVER_H
/**
 * @brief Initializes UART_2 pins, installs driver, creates separate tasks for tx and rx.
 * 
 * @note aborts if any of the uart calls fails.
 * 
 * TODO: graceful error handling.
 */
void init_kbus_uart_driver();
#endif //KBUS_UART_DRIVER_H