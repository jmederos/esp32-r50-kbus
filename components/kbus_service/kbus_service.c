#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "kbus_uart_driver.h"
#include "kbus_service.h"

#define HERTZ(hz) ((1000/hz) / portTICK_RATE_MS)
#define SECONDS(sec) ((sec*1000) / portTICK_RATE_MS)

static const char* TAG = "kbus_service";

void kbus_rx_service(uint8_t* data);

void init_kbus_service() {
    init_kbus_uart_driver(&kbus_rx_service, 20); //TODO: Configurable polling rate
}

void kbus_rx_service(uint8_t* data) {
    ESP_LOGI(TAG, "data from driver:\t%0x %0x", data[0], data[1]);
}

static void cdc_emulator() {
    static const char *cdc_emu_tag = "CDC_EMU";
    char *cdc_msg = (char*) malloc(6);
    cdc_msg = (char[]){0x18, 0x04, 0xFF, 0x02, 0x00, 0xE1}; // CD Changer Message http://web.archive.org/web/20110320053244/http://ibus.stuge.se/CD_Changer
    ESP_LOGI(cdc_emu_tag, "cdc_msg address: %d\t%s\t%0x", (int)cdc_msg, cdc_msg, cdc_msg[0]);
    ESP_LOGI(cdc_emu_tag, "Sending CD Changer Active Message: %s", cdc_msg);
    while(1) {
        kbus_send_bytes(cdc_emu_tag, cdc_msg, 6);
        vTaskDelay(SECONDS(10));
    }
}

void begin_cdc_emulator() {
    ESP_LOGI(TAG, "Creating CD Changer Emulator");
    xTaskCreatePinnedToCore(cdc_emulator, "cdc_emu", 4096, NULL, configMAX_PRIORITIES, NULL, 1);
}