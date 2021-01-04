#ifndef KBUS_SERVICE_H
#define KBUS_SERVICE_H
void init_kbus_service(QueueHandle_t bluetooth_queue);
void send_dev_ready(uint8_t source, uint8_t dest, bool startup);
#endif //KBUS_SERVICE_H
