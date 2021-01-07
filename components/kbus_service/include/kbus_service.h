#ifndef KBUS_SERVICE_H
#define KBUS_SERVICE_H
void init_kbus_service(QueueHandle_t bt_command_q, QueueHandle_t bt_track_info_q);
void send_dev_ready(uint8_t source, uint8_t dest, bool startup);
#endif //KBUS_SERVICE_H
