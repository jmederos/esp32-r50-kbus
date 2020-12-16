#ifndef AVRCP_CONTROL_H
#define AVRCP_CONTROL_H

#include <inttypes.h>
#include <stdint.h>

/* Setup AVRCP service */
int avrcp_setup(void);

uint8_t avrcp_ctl_next();

uint8_t avrcp_ctl_prev();

uint8_t avrcp_ctl_start_ff();

uint8_t avrcp_ctl_start_rwd();

uint8_t avrcp_ctl_end_long_press();

#endif // AVRCP_CONTROL_H