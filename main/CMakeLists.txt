idf_component_register(
        SRCS "main.c"
        INCLUDE_DIRS "../components/common"
        REQUIRES btstack wifi_service bt_services avrcp_control_driver kbus_service kbus_uart_driver
        )