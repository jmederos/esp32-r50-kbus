// C stdlib includes
#include <stddef.h>
#include <stdio.h>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
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
#include "kbus_service.h"
#include "bt_common.h"

#define SECONDS(sec) ((sec*1000) / portTICK_RATE_MS)

// TODO: Add these as menuconfig items
#define R50_BT_ENABLED
// #define R50_WIFI_ENABLED

// ! Debug Flags
// #define TASK_DEBUG

static const char* TAG = "r50-main";
static QueueHandle_t bt_cmd_queue, bt_info_queue;

#ifdef TASK_DEBUG
static void watcher_task(){
    const size_t bytes_per_task = 40;
    char *task_list_buffer = NULL;
    vTaskDelay(SECONDS(5));

    while(1){
        task_list_buffer = (char*) malloc(uxTaskGetNumberOfTasks() * bytes_per_task);
        if (task_list_buffer == NULL) {
            ESP_LOGE(TAG, "failed to allocate buffer for vTaskList output");
            abort();
        }

        vTaskList(task_list_buffer);
        printf("\n%sTask\t\tStat\tPrity\tHWM\tTsk#\tCPU%s\n", "\033[1m\033[4m\033[44;1m\033[K", LOG_RESET_COLOR);
        printf("%s", task_list_buffer);

        vTaskGetRunTimeStats(task_list_buffer);
        printf("%sTask\t\tAbs Time\t\tUsage%%%s\n", "\033[1m\033[4m\033[42m\033[K", LOG_RESET_COLOR);
        printf("%s", task_list_buffer);

        free(task_list_buffer);
        vTaskDelay(SECONDS(120));
    }
}

static void create_watcher_task(){
    int task_ret = xTaskCreate(watcher_task, "task_watcher", 4096, NULL, 5, NULL);
    if(task_ret != pdPASS){ESP_LOGE(TAG, "task_watcher creation failed with: %d", task_ret);}
}
#endif

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

#ifdef TASK_DEBUG
    ESP_LOGI(TAG, "Creating Task Watcher");
    create_watcher_task();
#endif

    // Setup bluetooth command queue
    bt_cmd_queue = xQueueCreate(4, sizeof(bt_cmd_type_t));
    // Setup bluetooth "now playing" queue
    bt_info_queue = xQueueCreate(2, sizeof(bt_now_playing_info_t));

    // Setup kbus service; has side-effect of initializing and starting UART driver.
    init_kbus_service(bt_cmd_queue, bt_info_queue);

#ifdef R50_WIFI_ENABLED // Gating wifi and bt since there's still issues with them running concurrently.
    wifi_init_softap();
#endif

#ifdef R50_BT_ENABLED
    ESP_LOGI(TAG, "Starting bt services...");
    bluetooth_services_setup(bt_cmd_queue, bt_info_queue);
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
