idf_component_register(
        SRCS "wifi_service.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES nvs_flash
        )