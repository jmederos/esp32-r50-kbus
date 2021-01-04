idf_component_register(SRCS "sdrs_emulator.c"
                    INCLUDE_DIRS "include" "../common"
                    REQUIRES kbus_uart_driver kbus_service)
