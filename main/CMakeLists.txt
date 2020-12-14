idf_component_register(
        SRCS "main.c"
        REQUIRES wifi_service bt_services avrcp_control btstack kbus_uart_driver kbus_service
        )