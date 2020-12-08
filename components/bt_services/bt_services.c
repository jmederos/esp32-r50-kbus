#include "esp_log.h"

#include "btstack.h"
#include "btstack_port_esp32.h"
#include "bt_services.h"
#include "avrcp_control.h"

static const char* TAG = "r50-btstack-services";

int bluetooth_services_setup(void){    
    // optional: enable packet logger
    // hci_dump_open(NULL, HCI_DUMP_STDOUT);

    // Configure BTstack for ESP32 VHCI Controller
    btstack_init();

    // Initialize L2CAP
    l2cap_init();
    // Initialize SDP
    sdp_init();
    // Setup avrcp
    avrcp_setup();

    // Turn on bluetooth
    ESP_LOGI(TAG, "Bluetooth HCI on");
    hci_power_control(HCI_POWER_ON);

    return 0;
}
