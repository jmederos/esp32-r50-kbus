idf_component_register(SRCS "kbus_service.c"
                    INCLUDE_DIRS "include" "../common"
                    REQUIRES kbus_uart_driver)
