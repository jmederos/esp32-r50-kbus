#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "kbus_uart_driver.h"
#include "kbus_service.h"
#include "kbus_defines.h"

#define HERTZ(hz) ((1000/hz) / portTICK_RATE_MS)
#define SECONDS(sec) ((sec*1000) / portTICK_RATE_MS)

static const char* TAG = "kbus_service";

void kbus_rx_service(uint8_t* data, uint8_t rx_bytes);
void encode_kbus_message(uint8_t src, uint8_t dst, uint8_t data[], uint8_t data_len, char* encoded_msg);

void init_kbus_service() {
    init_kbus_uart_driver(&kbus_rx_service, 20); //TODO: Configurable polling rate
}

void kbus_rx_service(uint8_t* data, uint8_t rx_bytes) {
    ESP_LOGI(TAG, "data from driver:");
    ESP_LOG_BUFFER_HEXDUMP(TAG, data, rx_bytes, ESP_LOG_INFO);
}

static void cdc_emulator() {
    static const char *cdc_emu_tag = "CDC_EMU";
    char *cdc_msg = (char*) malloc(6);
    
    ESP_LOGD(cdc_emu_tag, "Sending CD Changer Startup Message: %s", cdc_msg);
    //* CD Changer Messages from http://web.archive.org/web/20110320053244/http://ibus.stuge.se/CD_Changer
    encode_kbus_message(CDC, LOC, (uint8_t[]){0x02, 0x01}, 2, cdc_msg); // Encode "Device status Ready After Reset"
    kbus_send_bytes(cdc_emu_tag, cdc_msg, 6);                           // Send "After Reset" message on boot
    encode_kbus_message(CDC, LOC, (uint8_t[]){0x02, 0x00}, 2, cdc_msg); // Encode "Device status Ready"

    while(1) {
        ESP_LOGD(cdc_emu_tag, "Sending CD Changer Reply Message: %s", cdc_msg);
        ESP_LOG_BUFFER_HEXDUMP(cdc_emu_tag, cdc_msg, 6, ESP_LOG_DEBUG);
        kbus_send_bytes(cdc_emu_tag, cdc_msg, 6); // Send "Device status Ready" every 20 seconds
        vTaskDelay(SECONDS(20));
    }
}

void begin_cdc_emulator() {
    ESP_LOGI(TAG, "Creating CD Changer Emulator");
    xTaskCreatePinnedToCore(cdc_emulator, "cdc_emu", 4096, NULL, configMAX_PRIORITIES, NULL, 1);
}

void encode_kbus_message(uint8_t src, uint8_t dst, uint8_t data[], uint8_t data_len, char* encoded_msg) {

    uint8_t msg_len = data_len + 2; //Base message length + destination & checksum bytes.

    uint8_t checksum = 0x00;
    checksum ^= src ^ dst ^ msg_len;

    for(uint8_t i = 0; i < data_len; i++) {
        checksum ^= data[i];
    }

    encoded_msg[0] = (char) src;
    encoded_msg[1] = (char) msg_len;
    encoded_msg[2] = (char) dst;
    encoded_msg[3+data_len] = (char) checksum;

    for(uint8_t i = 0; i < data_len; i++) {
        encoded_msg[3+i] = (char) data[i];
    }

    ESP_LOGD(TAG, "%02x %02x %02x %02x %02x %02x", src, msg_len, dst, data[0], data[1], checksum);
}

void decode_kbus_message(uint8_t* data, uint8_t rx_bytes) {

}