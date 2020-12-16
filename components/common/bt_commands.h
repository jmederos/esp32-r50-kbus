#ifndef BT_COMMANDS_H
#define BT_COMMANDS_H

typedef enum {
    BT_CMD_NOOP = 0x00,
    AVRCP_PLAY,
    AVRCP_STOP,
    AVRCP_FF_START,
    AVRCP_FF_STOP,
    AVRCP_RWD_START,
    AVRCP_RWD_STOP,
    AVRCP_NEXT,
    AVRCP_PREV
} bt_cmd_type_t;

#endif
