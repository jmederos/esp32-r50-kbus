#include "btstack.h"
#include "btstack_main.h"
#include "avrcp_control.h"

int btstack_main(int argc, const char * argv[]){
    UNUSED(argc);
    (void)argv;
    
    // Initialize L2CAP
    l2cap_init();
    // Initialize SDP
    sdp_init();

    avrcp_setup();

    // turn on!
    printf("Starting BTstack ...\n");
    hci_power_control(HCI_POWER_ON);
    return 0;
}
