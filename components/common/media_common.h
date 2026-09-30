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

// making compatible with AMS PlaybackState
typedef enum {
    MEDIA_PAUSED,    // AMS PlaybackStatePaused
    MEDIA_PLAYING,   // AMS PlaybackStatePlaying
    MEDIA_SEEK_RWD,  // AMS PlaybackStateRewinding
    MEDIA_SEEK_FWD,  // AMS PlaybackStateFastForwarding
    MEDIA_STOPPED,   // Media is stopped; was used on AVRCP
    DEV_DISCONNECTED, // Device is disconnected
    DEV_CONNECTING,   // Device is connecting
    DEV_CONNECTED     // Device is connected
} media_state_t;

typedef struct {
    char* album_name;
    char* track_title;
    char* artist_name;
    char* app_name;
    char state_detail[32];

    uint32_t track_len_ms;
    uint8_t cur_track;
    uint8_t total_tracks;
    media_state_t* state;
} media_now_playing_t;

#endif
