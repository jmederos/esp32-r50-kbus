#ifndef BT_COMMON_H
#define BT_COMMON_H

typedef enum {
    BT_CMD_NOOP = 0x00,
    BT_CONNECT,
    BT_DISCONNECT,
    AVRCP_PLAY,
    AVRCP_PAUSE,
    AVRCP_STOP,
    AVRCP_FF_START,
    AVRCP_FF_STOP,
    AVRCP_RWD_START,
    AVRCP_RWD_STOP,
    AVRCP_NEXT,
    AVRCP_PREV,
    AVRCP_GET_INFO
} bt_cmd_type_t;

typedef struct {
    char album_name[128];
    char track_title[128];
    char artist_name[64];
    char playback_state[16];

    uint32_t track_len_ms;
    uint8_t cur_track;
    uint8_t total_tracks;
} bt_now_playing_info_t;

#endif
