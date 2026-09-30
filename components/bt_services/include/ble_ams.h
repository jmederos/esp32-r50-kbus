#ifndef BLE_AMS_H
#define BLE_AMS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/ble_hs.h"

//RemoteCommandID values
typedef enum {
   RemoteCommandIDPlay                =  0,
   RemoteCommandIDPause               =  1,
   RemoteCommandIDTogglePlayPause     =  2,
   RemoteCommandIDNextTrack           =  3,
   RemoteCommandIDPreviousTrack       =  4,
   RemoteCommandIDVolumeUp            =  5,
   RemoteCommandIDVolumeDown          =  6,
   RemoteCommandIDAdvanceRepeatMode   =  7,
   RemoteCommandIDAdvanceShuffleMode  =  8,
   RemoteCommandIDSkipForward         =  9,
   RemoteCommandIDSkipBackward        = 10,
   RemoteCommandIDLikeTrack           = 11,
   RemoteCommandIDDislikeTrack        = 12,
   RemoteCommandIDBookmarkTrack       = 13,
   //Reserved EventID values = 14-255
} ble_ams_RemoteCommandID;

//EntityID values
typedef enum {
   EntityIDPlayer = 0,
   EntityIDQueue  = 1,
   EntityIDTrack  = 2,
   //Reserved EventID values = 3–255
} ble_ams_EntityID;

//EntityUpdateFlags
typedef enum {
   EntityUpdateFlagTruncated    = (1 << 0),
   //Reserved EventFlags (1 << 1) to (1 << 7)
}ble_ams_EntityUpdateFlags;

// PlayerAttributeID values
typedef enum {
   PlayerAttributeIDName           = 0, // A string containing the localized name of the app.
   PlayerAttributeIDPlaybackInfo   = 1, // A concatenation of three comma-separated values: PlaybackState, PlaybackRate, ElapsedTime
   PlayerAttributeIDVolume         = 2, // A string that represents the floating point value of the volume, ranging from 0 (silent) to 1 (full volume).
   //Reserved PlayerAttributeID values  = 3-255
} ble_ams_PlayerAttributeID ;

//QueueAttributeID values
typedef enum {
   QueueAttributeIDIndex       = 0,
   QueueAttributeIDCount       = 1,
   QueueAttributeIDShuffleMode = 2,
   QueueAttributeIDRepeatMode  = 3,
   //Reserved QueueAttributeID values = 4–255
} ble_ams_QueueAttributeID;

//TrackAttributeID values
typedef enum {
   TrackAttributeIDArtist      = 0,
   TrackAttributeIDAlbum       = 1,
   TrackAttributeIDTitle       = 2,
   TrackAttributeIDDuration    = 3,
   //Reserved TrackAttributeID  values = 4–255
} ble_ams_TrackAttributeID;

// PlaybackState
typedef enum {
   PlaybackStatePaused         = 0,
   PlaybackStatePlaying,
   PlaybackStateRewinding,
   PlaybackStateFastForwarding
} ble_ams_PlaybackState;

// ShuffleMode
typedef enum {
   ShuffleModeOff     = 0,
   ShuffleModeOne,
   ShuffleModeAll
} ble_ams_ShuffleMode;

// RepeatMode
typedef enum {
   RepeatModeOff      = 0,
   RepeatModeOne,
   RepeatModeAll
} ble_ams_RepeatMode;


typedef enum {
   InvalidState     = (0xA0), //The MR has not properly set up the AMS, e.g. it wrote to the Entity Update or Entity Attribute characteristic without subscribing to GATT notifications for the Entity Update characteristic.
   InvalidCommand   = (0xA1), // The command was improperly formatted.
   AbsentAttribute  = (0xA2), // The corresponding attribute is empty.
} ble_ams_error_code;


// AMS UUID: 89D3502B-0F36-433A-8EF4-C502AD55F8DC
static  ble_uuid128_t apple_media_service_uuid = BLE_UUID128_INIT(
            0xdc, 0xf8, 0x55, 0xad, 0x02, 0xc5, 0xf4, 0x8e, 0x3a, 0x43, 0x36, 0x0f, 0x2b, 0x50, 0xd3, 0x89);

// In its basic form, the AMS exposes three characteristics:
// Remote Command: UUID 9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2 (writeable, notifiable)
static   ble_uuid128_t ams_remote_command_uuid = BLE_UUID128_INIT(
        0xc2, 0x51, 0xca, 0xf7, 0x56, 0x0e, 0xdf, 0xb8, 0x8a, 0x4a, 0xb1, 0x57, 0xd8, 0x81, 0x3c, 0x9b);
// Entity Update: UUID 2F7CABCE-808D-411F-9A0C-BB92BA96C102 (writeable with response, notifiable)
static   ble_uuid128_t ams_entity_update_uuid = BLE_UUID128_INIT(
        0x02, 0xc1, 0x96, 0xba, 0x92, 0xbb, 0x0c, 0x9a, 0x1f, 0x41, 0x8d, 0x80, 0xce, 0xab, 0x7c, 0x2f);
// Entity Attribute: UUID C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7 (readable, writeable)
static   ble_uuid128_t ams_entity_attr_uuid = BLE_UUID128_INIT(
        0xd7, 0xd5, 0xbb, 0x70, 0xa8, 0xa3, 0xab, 0xa6, 0xd8, 0x46, 0xab, 0x23, 0x8c, 0xf3, 0xb2, 0xc6);


// attribute handles
extern uint16_t ams_remote_command_handle;
extern uint16_t ams_entity_update_handle;
extern uint16_t ams_entity_attr_handle;
extern uint16_t shared_e_handle;
extern bool ams_setup_complete;

// defined in main
extern int find_ancs_ams(uint16_t conn_handle);

// service discovery callback
int ams_service_discovered_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *svc, void *arg);

// helper functions
char *EntityID_to_String(uint8_t EntityID);
char *PlayerAttrID_to_String(uint8_t PlayerAttr);
char *QueueAttrID_to_String(uint8_t QueueAttr);
char *TrackAttrID_to_String(uint8_t TrackAttr);

// message decoding functions
void ams_decode_entity_update(uint8_t *message, uint16_t message_len);
int  ams_send_remote_command(uint16_t conn_handle, ble_ams_RemoteCommandID command);
void ams_decode_entity_attr(uint8_t *message, uint16_t message_len);

// getters
char* ams_get_track_str();
char* ams_get_album_str();
char* ams_get_artist_str();
char* ams_get_app_str();
uint8_t* ams_get_playback_state();

#endif