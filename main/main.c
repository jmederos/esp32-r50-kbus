/*
 * Copyright (C) 2020 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY MATTHIAS RINGWALD AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

// C stdlib includes
#include <stddef.h>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"

// esp-idf includes
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

// btstack includes
#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "hci_dump.h"

// component includes
#include "bt_services.h"
#include "wifi_service.h"
#include "kbus_uart_driver.h"

// TODO: Add these as menuconfig items
// #define R50_BT_ENABLED
#define R50_WIFI_ENABLED

static const char* TAG = "r50-main";

static void sample_worker(void *pvParameter){
    (void)(sizeof(pvParameter));
    static int counter = 0;
    while(1){
        printf("HTTP task goes here... - counter %u\n", counter++);
        vTaskDelay(120000 / portTICK_PERIOD_MS);
    }
}

void create_server_task(void){
    static const char* TAG = "r50-srvTsk";
    ESP_LOGI(TAG, "Creating task");

    int srvTaskRet = xTaskCreatePinnedToCore(&sample_worker, "r50-bts-loop", 4096, NULL, 5, NULL, 1);

    if(srvTaskRet != pdPASS) {
        ESP_LOGE(TAG, "Task creation failed with: %d", srvTaskRet);
    }
}

static void initNVS(){
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}


int app_main(void){
    initNVS();

    init_kbus_uart_driver();

    #ifdef R50_WIFI_ENABLED
    create_server_task();
    wifi_init_softap();
    #endif

    #ifdef R50_BT_ENABLED
    ESP_LOGI(TAG, "Starting bt services...");
    bluetooth_services_setup();
    // Running btstack_run_loop_execute() as it's own task or in a wrapper wasn't working;
    // however, does work as lowest priority loop after other tasks. Going with this.
    ESP_LOGI(TAG, "btstack run loop");
    btstack_run_loop_execute();
    #else
    while(1) {
        printf("Bluetooth runloop goes here...\n");
        vTaskDelay(600000 / portTICK_PERIOD_MS);
    }
    #endif

    return 0;
}
