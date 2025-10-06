idf_component_register(
    SRCS "bt_services.c" "ble_ams.c" "ble_ancs.c"
    INCLUDE_DIRS "include" "../common"
    REQUIRES bt nvs_flash
)
