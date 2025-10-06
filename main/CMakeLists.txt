idf_component_register(
        SRCS "main.c"
        INCLUDE_DIRS "."
        REQUIRES nvs_flash bt_services kbus_service kbus_uart_driver
)