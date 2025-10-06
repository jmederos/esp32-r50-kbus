idf_component_register(
        SRCS "wifi_service.c"
        INCLUDE_DIRS "include"
        REQUIRES nvs_flash esp_wifi esp_event esp_hw_support lwip
)