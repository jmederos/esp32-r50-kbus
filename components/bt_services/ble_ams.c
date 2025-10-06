#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "ble_ams.h"

#define NimBLE_AMS_TAG  "NimBLE_AMS"

uint16_t ams_remote_command_handle;
uint16_t ams_entity_update_handle;
uint16_t ams_entity_attr_handle;

char *EntityID_to_String(uint8_t EntityID)
{
    char *str = NULL;
    switch (EntityID)
    {
        case EntityIDPlayer:
            str = "Player Update";
            break;
        case EntityIDQueue:
            str = "Queue Update";
            break;
        case EntityIDTrack:
            str = "Track Update";
            break;
        default:
            str = "unknown EntityID";
            break;
    }
    return str;
}

char *PlayerAttrID_to_String(uint8_t PlayerAttr)
{
    char *str = NULL;
    switch(PlayerAttr) {
        case PlayerAttributeIDName:
            str = "Application Name";
            break;
        case PlayerAttributeIDPlaybackInfo:
            str = "Playback Info";
            break;
        case PlayerAttributeIDVolume:
            str = "Volume";
            break;
        default:
            str = "Unknown PlayerAttributeID";
            break;
    }
    return str;
}

char *PlaybackState_to_String(uint8_t PlaybackState)
{
    char *str = NULL;
    switch(PlaybackState) {
        case PlaybackStatePaused:
            str = "Paused";
            break;
        case PlaybackStatePlaying:
            str = "Playing";
            break;
        case PlaybackStateRewinding:
            str = "<< RWD";
            break;
        case PlaybackStateFastForwarding:
            str = "FF >>";
            break;
        default:
            str = "Unknown PlayerAttributeID";
            break;
    }
    return str;
}

char *QueueAttrID_to_String(uint8_t QueueAttr)
{
    char *str = NULL;
    switch(QueueAttr) {
        case QueueAttributeIDIndex:
            str = "Queue Index";
            break;
        case QueueAttributeIDCount:
            str = "Queue Size";
            break;
        case QueueAttributeIDShuffleMode:
            str = "Shuffle Mode";
            break;
        case QueueAttributeIDRepeatMode:
            str = "Repeat Mode";
            break;
        default:
            str = "Unknown QueueAttributeID";
            break;
    }
    return str;
}

char *TrackAttrID_to_String(uint8_t TrackAttr)
{
    char *str = NULL;
    switch(TrackAttr) {
        case TrackAttributeIDArtist:
            str = "Artist Name";
            break;
        case TrackAttributeIDAlbum:
            str = "Album Name";
            break;
        case TrackAttributeIDTitle:
            str = "Track Title";
            break;
        case TrackAttributeIDDuration:
            str = "Track Duration (float) in sec";
            break;
        default:
            str = "Unknown TrackAttributeID";
            break;
    }
    return str;
}

static char *Errcode_to_String(uint16_t status)
{
    char *Errstr = NULL;
    switch (status) {
        case InvalidState:
            Errstr = "InvalidState: MR attempted to update characteristic without subscribing";
            break;
        case InvalidCommand:
            Errstr = "InvalidCommand: improperly formatted command";
            break;
        case AbsentAttribute:
            Errstr = "AbsentAttribute: corresponding attribute is empty";
            break;
        default:
            Errstr = "unknown_ams_error_code";
            break;
    }
    return Errstr;
}

static int ams_mark_as_complete(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr,
                        void *arg)
{
    int rc =0;
    ams_setup_complete = true;
    rc = find_ancs_ams(conn_handle);
    return rc;
}

static int ams_request_player_info(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr,
                        void *arg)
{
    int rc = 0;
    uint8_t data[3] = {EntityIDPlayer, PlayerAttributeIDPlaybackInfo, PlayerAttributeIDName}; // Request Track notifications

    MODLOG_DFLT(INFO,
                "Subscribed to EntityIDPlayer; "
                "status=%d conn_handle=%d",
                error->status, conn_handle);

    rc = ble_gattc_write_flat(conn_handle,
                        ams_entity_update_handle,
                        data, sizeof(data),
                        ams_mark_as_complete, NULL);

    return rc;
}

// TODO: generalize to take argument array for data to avoid multiple callbacks
static int ams_request_track_info(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr,
                        void *arg)
{
    int rc = 0;
    uint8_t data[4] = {EntityIDTrack, TrackAttributeIDArtist, TrackAttributeIDTitle, TrackAttributeIDAlbum}; // Request Track notifications

    MODLOG_DFLT(INFO,
                "Subscribed to EntityIDTrack; "
                "status=%d conn_handle=%d",
                error->status, conn_handle);

    rc = ble_gattc_write_flat(conn_handle,
                        ams_entity_update_handle,
                        data, sizeof(data),
                        ams_request_player_info, NULL);

    return rc;
}

static int ams_dsc_discovered_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                                uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                                void *arg)
{
    int rc = 0;
    uint8_t cccd_val[2] = {0x01, 0x00}; // Enable notification

    if (error->status == 0) {
        MODLOG_DFLT(INFO, "ams_dsc_discovered_cb: gatt_dsc: %04x", dsc->uuid.u32);
        if (ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            rc = ble_gattc_write_flat(conn_handle,
                                    dsc->handle,
                                    cccd_val, sizeof(cccd_val),
                                    ams_request_track_info, NULL);
            if (rc != 0) {
                MODLOG_DFLT(WARN, "Failed to subscribe to (rc=%d)", rc);
                return rc;
            }
        }
    } else {
        rc = error->status;
        MODLOG_DFLT(ERROR, "ams_dsc_cb ERROR: 0x%04x %s", rc, Errcode_to_String(rc));
    }
    return rc;
}

static int ams_chr_discovered_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                    const struct ble_gatt_chr *chr, void *arg)
{
    int rc = 0;
    if (error->status == 0) {

        MODLOG_DFLT(INFO, "ams_chr_uuid: %x \tprop: %08x", chr->uuid.u32, chr->properties);

        /* Entity Update */
        if ((chr->properties & BLE_GATT_CHR_PROP_NOTIFY) &&
                 ble_uuid_cmp(&chr->uuid.u, &ams_entity_update_uuid.u) == 0) {

            ams_entity_update_handle = chr->val_handle;
            rc = ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, shared_e_handle, ams_dsc_discovered_cb, NULL);
            
            MODLOG_DFLT(WARN, "Found Entity Update characteristic");
        }

        /* Remote Command */
        else if ((chr->properties & BLE_GATT_CHR_PROP_NOTIFY) &&
             ble_uuid_cmp(&chr->uuid.u, &ams_remote_command_uuid.u) == 0) {

            ams_remote_command_handle = chr->val_handle;
            // rc = ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, shared_e_handle, ams_dsc_discovered_cb, NULL);
            MODLOG_DFLT(WARN, "Found Remote Command characteristic");
        }

        /* Entity Attribute */
        else if ((chr->properties & BLE_GATT_CHR_PROP_WRITE) &&
                 ble_uuid_cmp(&chr->uuid.u, &ams_entity_attr_uuid.u) == 0) {

            ams_entity_attr_handle = chr->val_handle;
            MODLOG_DFLT(WARN, "Found Entity Attribute characteristic");
        }
        else {
            MODLOG_DFLT(WARN, "Found unknown characteristic: %x", chr->uuid.u32);
        }
    } else{
        rc = error->status;
        MODLOG_DFLT(ERROR, "ams_chr_cb ERROR: 0x%04x %s", rc, Errcode_to_String(rc));
    }
    return rc;
}

int ams_service_discovered_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *svc, void *arg)
{
    int rc;
    if (error->status == 0) {
        MODLOG_DFLT(WARN, "svc_uuid: %x", svc->uuid.u32);
        MODLOG_DFLT(WARN, "svc_start_handle: %08x svc_end_handle: %08x", svc->start_handle, svc->end_handle);

        if (svc == NULL) {
            MODLOG_DFLT(ERROR, "AMS service not found");
            return -1;
        }

        shared_e_handle = svc->end_handle;
        /* discover characteristics within this service range */
        rc = ble_gattc_disc_all_chrs(conn_handle, svc->start_handle, svc->end_handle, ams_chr_discovered_cb, NULL);
    } else {
        rc = error->status;
        MODLOG_DFLT(ERROR, "ams_service_cb ERROR: 0x%04x %s", rc, Errcode_to_String(rc));
    }
    return rc;
}

void ams_decode_entity_update(uint8_t *message, uint16_t message_len)
{
    if (!message || message_len < 5) {
        return;
    }
    
    uint8_t entityID          = message[0];
    uint8_t attrID            = message[1];
    uint8_t entityUpdateFlags = message[2];
    char    value[96]         = {'\0'};          // initialize to \0
    memcpy(value, &message[3], message_len - 3); // copy length-3 bytes

    switch (entityID) {
        case EntityIDPlayer:
            if(attrID == PlayerAttributeIDPlaybackInfo) {
                ESP_LOGI(NimBLE_AMS_TAG, "%d bytes\t%s - %s\t%s\t%s - 0x%01x",
                message_len,
                EntityID_to_String(entityID),
                PlayerAttrID_to_String(attrID),
                PlaybackState_to_String(atoi(&value[0])),
                value, entityUpdateFlags
            );
            } else {
                ESP_LOGI(NimBLE_AMS_TAG, "%d bytes\t%s - %s\t%s - 0x%01x",
                message_len,
                EntityID_to_String(entityID),
                PlayerAttrID_to_String(attrID),
                value, entityUpdateFlags
            );
            }
            break;

        case EntityIDQueue:
            ESP_LOGI(NimBLE_AMS_TAG, "%d bytes\t%s - %s\t%s - 0x%01x",
                message_len,
                EntityID_to_String(entityID),
                QueueAttrID_to_String(attrID),
                value, entityUpdateFlags
            );
            break;

        case EntityIDTrack:
            ESP_LOGI(NimBLE_AMS_TAG, "%d bytes\t%s - %s\t%s - 0x%01x",
                message_len,
                EntityID_to_String(entityID),
                TrackAttrID_to_String(attrID),
                value, entityUpdateFlags
            );
            break;

        default:
            break;
    }

}

int ams_send_remote_command(uint16_t conn_handle, ble_ams_RemoteCommandID command) {

    int rc = ble_gattc_write_flat(conn_handle,
                                  ams_remote_command_handle,
                                  &command,
                                  1,
                                  NULL, // no callback
                                  NULL); // no callback argument

    if (rc != 0) {
        MODLOG_DFLT(WARN, "Failed to write AMS command: rc=%d\n", rc);
    }

    return rc;
}

void ams_decode_entity_attr(uint8_t *message, uint16_t message_len)
{
    if (!message || message_len < 2) {
        return;
    }

    uint8_t entityID      = message[0];
    uint8_t attrID        = message[1];

    switch (entityID) {
        case EntityIDPlayer:
            ESP_LOGI(NimBLE_AMS_TAG, "%d bytes\t%s - %s",
                message_len,
                EntityID_to_String(entityID),
                PlayerAttrID_to_String(attrID)
            );
            break;

        case EntityIDQueue:
            ESP_LOGI(NimBLE_AMS_TAG, "%d bytes\t%s - %s",
                message_len,
                EntityID_to_String(entityID),
                QueueAttrID_to_String(attrID)
            );
            break;

        case EntityIDTrack:
            ESP_LOGI(NimBLE_AMS_TAG, "%d bytes\t%s - %s",
                message_len,
                EntityID_to_String(entityID),
                TrackAttrID_to_String(attrID)
            );
            break;

        default:
            break;
    }
}