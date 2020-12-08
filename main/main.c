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

#include <stddef.h>

#include "esp_system.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"

#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "hci_dump.h"

#include "bt_services.h"

static const char* TAG = "r50-main";

static void sample_worker(void *pvParameter){
    (void)(sizeof(pvParameter));
    static int counter = 0;
    while(1){
        printf("BTstack runs in other task - counter %u\n", counter++);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void create_server_task(void){
    static const char* TAG = "r50-srvTsk";
    ESP_LOGI(TAG, "Creating task");

    int btTaskReturn = -1;

    btTaskReturn = xTaskCreate(&sample_worker, "r50-bts-loop", 4096, NULL, 5, NULL);

    if(btTaskReturn != pdPASS) {
        ESP_LOGE(TAG, "Task creation failed with: %d", btTaskReturn);
    }
}


int app_main(void){
    // ESP_LOGI(TAG, "init NVS");
    // nvs_flash_init();

    create_server_task();

    ESP_LOGI(TAG, "Starting bt services...");
    bluetooth_services_setup();

    // Running btstack_run_loop_execute() as it's own task or in a wrapper wasn't working;
    // however, does work as lowest priority loop after other tasks. Going with this.
    ESP_LOGI(TAG, "btstack run loop");
    btstack_run_loop_execute();

    return 0;
}
