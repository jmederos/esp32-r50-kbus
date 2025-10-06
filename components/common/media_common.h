#ifndef MEDIA_COMMON_H
#define MEDIA_COMMON_H

typedef enum {
    BT_CMD_NOOP = 0x00,
    BT_CONNECT,
    BT_DISCONNECT,
    MEDIA_PLAY,
    MEDIA_PAUSE,
    MEDIA_STOP,
    MEDIA_FF_START,
    MEDIA_FF_STOP,
    MEDIA_RWD_START,
    MEDIA_RWD_STOP,
    MEDIA_NEXT,
    MEDIA_PREV,
    MEDIA_REQ_INFO
} media_cmd_type_t;

// making compatible with avrcp.c
// avrcp_play_status_name
typedef enum {
    MEDIA_STOPPED,
    MEDIA_PLAYING,
    MEDIA_PAUSED,
    MEDIA_SEEK_FWD,
    MEDIA_SEEK_RWD,
    DEV_DISCONNECTED,
    DEV_CONNECTING,
    DEV_CONNECTED
} media_state_t;

typedef struct {
    char album_name[128];
    char track_title[128];
    char artist_name[64];
    char state_detail[32];

    uint32_t track_len_ms;
    uint8_t cur_track;
    uint8_t total_tracks;
    media_state_t state;
} media_now_playing_t;

#endif
