#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btstack.h"

#include "avrcp_control.h"

static int volume_percentage = 0; 

static uint8_t events_num = 3;
static uint8_t events[] = {
    AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED,
    AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED,
    AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED
};

static uint8_t companies_num = 2;
static uint8_t companies[] = {
    BLUETOOTH_COMPANY_ID_ERICSSON_TECHNOLOGY_LICENSING,
    BLUETOOTH_COMPANY_ID_APPLE_INC
};

static btstack_packet_callback_registration_t hci_event_callback_registration;

static uint8_t  sdp_avrcp_target_service_buffer[150];
static uint8_t  sdp_avrcp_controller_service_buffer[200];
static uint8_t  device_id_sdp_service_buffer[100];

static uint16_t avrcp_cid = 0;
static uint8_t  avrcp_connected = 0;
static uint8_t  avrcp_subevent_value[100];

/* Setup AVRCP service */
int avrcp_setup(void);
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void avrcp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void avrcp_controller_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void avrcp_target_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void avrcp_volume_changed(uint8_t volume);


int avrcp_setup(void){
    
    // Initialize AVRCP service
    avrcp_init();
    avrcp_register_packet_handler(&avrcp_packet_handler);
    
    // Initialize AVRCP Controller
    avrcp_controller_init();
    avrcp_controller_register_packet_handler(&avrcp_controller_packet_handler);
    
     // Initialize AVRCP Target
    // avrcp_target_init();
    // avrcp_target_register_packet_handler(&avrcp_target_packet_handler);

    // Create AVRCP Controller service record and register it with SDP
    memset(sdp_avrcp_controller_service_buffer, 0, sizeof(sdp_avrcp_controller_service_buffer));
    uint16_t controller_supported_features = AVRCP_FEATURE_MASK_CATEGORY_PLAYER_OR_RECORDER;

#ifdef AVRCP_BROWSING_ENABLED
    controller_supported_features |= AVRCP_FEATURE_MASK_BROWSING;
#endif
    avrcp_controller_create_sdp_record(sdp_avrcp_controller_service_buffer, 0x10002, controller_supported_features, NULL, NULL);
    sdp_register_service(sdp_avrcp_controller_service_buffer);
    
    // Create AVRCP Target service record and register it with SDP
    // memset(sdp_avrcp_target_service_buffer, 0, sizeof(sdp_avrcp_target_service_buffer));
    // uint16_t target_supported_features = AVRCP_FEATURE_MASK_CATEGORY_MONITOR_OR_AMPLIFIER;
    // avrcp_target_create_sdp_record(sdp_avrcp_target_service_buffer, 0x10003, target_supported_features, NULL, NULL);
    // sdp_register_service(sdp_avrcp_target_service_buffer);

    // Create Device ID (PnP) service record and register it with SDP
    memset(device_id_sdp_service_buffer, 0, sizeof(device_id_sdp_service_buffer));
    device_id_create_sdp_record(device_id_sdp_service_buffer, 0x10004, DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH, BLUETOOTH_COMPANY_ID_APPLE_INC, 1, 1);
    sdp_register_service(device_id_sdp_service_buffer);

    // Set local name with a template Bluetooth address, that will be automatically
    // replaced with a actual address once it is available, i.e. when BTstack boots
    // up and starts talking to a Bluetooth module.
    gap_set_local_name("R50 Control");
    gap_discoverable_control(1);
    gap_set_class_of_device(0x240418);

    // Register for HCI events
    // hci_event_callback_registration.callback = &hci_packet_handler;
    // hci_add_event_handler(&hci_event_callback_registration);

    return 0;
}
/* LISTING_END */

static void avrcp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);
    uint16_t local_cid;
    uint8_t  status = 0xFF;
    bd_addr_t adress;
    
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    switch (packet[2]){
        case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED: {
            local_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
            status = avrcp_subevent_connection_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS){
                printf("AVRCP: Connection failed: status 0x%02x\n", status);
                avrcp_cid = 0;
                return;
            }
            
            avrcp_cid = local_cid;
            avrcp_connected = 1;
            avrcp_subevent_connection_established_get_bd_addr(packet, adress);
            printf("AVRCP: Connected to %s, cid 0x%02x\n", bd_addr_to_str(adress), avrcp_cid);

            // automatically enable notifications
            avrcp_controller_enable_notification(avrcp_cid, AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
            avrcp_controller_enable_notification(avrcp_cid, AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED);
            avrcp_controller_enable_notification(avrcp_cid, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);
            return;
        }
        
        case AVRCP_SUBEVENT_CONNECTION_RELEASED:
            printf("AVRCP: Channel released: cid 0x%02x\n", avrcp_subevent_connection_released_get_avrcp_cid(packet));
            avrcp_cid = 0;
            avrcp_connected = 0;
            return;
        default:
            break;
    }
}

static void avrcp_controller_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);
    uint8_t  status = 0xFF;
    
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    
    status = packet[5];
    if (!avrcp_cid) return;

    // ignore INTERIM status
    if (status == AVRCP_CTYPE_RESPONSE_INTERIM){
        switch (packet[2]){
            case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_POS_CHANGED:{
                uint32_t playback_position_ms = avrcp_subevent_notification_playback_pos_changed_get_playback_position_ms(packet);
                if (playback_position_ms == AVRCP_NO_TRACK_SELECTED_PLAYBACK_POSITION_CHANGED){
                    printf("AVRCP Controller: playback position changed, no track is selected\n");
                }  
                break;
            }
            default:
                break;
        }
        return;
    } 
            
    memset(avrcp_subevent_value, 0, sizeof(avrcp_subevent_value));
    switch (packet[2]){
        case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_POS_CHANGED:
            printf("AVRCP Controller: Playback position changed, position %d ms\n", (unsigned int) avrcp_subevent_notification_playback_pos_changed_get_playback_position_ms(packet));
            break;
        case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_STATUS_CHANGED:
            printf("AVRCP Controller: Playback status changed %s\n", avrcp_play_status2str(avrcp_subevent_notification_playback_status_changed_get_play_status(packet)));
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_NOW_PLAYING_CONTENT_CHANGED:
            printf("AVRCP Controller: Playing content changed\n");
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_TRACK_CHANGED:
            printf("AVRCP Controller: Track changed\n");
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
            printf("AVRCP Controller: Absolute volume changed %d\n", avrcp_subevent_notification_volume_changed_get_absolute_volume(packet));
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_AVAILABLE_PLAYERS_CHANGED:
            printf("AVRCP Controller: Changed\n");
            return; 
        case AVRCP_SUBEVENT_SHUFFLE_AND_REPEAT_MODE:{
            uint8_t shuffle_mode = avrcp_subevent_shuffle_and_repeat_mode_get_shuffle_mode(packet);
            uint8_t repeat_mode  = avrcp_subevent_shuffle_and_repeat_mode_get_repeat_mode(packet);
            printf("AVRCP Controller: %s, %s\n", avrcp_shuffle2str(shuffle_mode), avrcp_repeat2str(repeat_mode));
            break;
        }
        case AVRCP_SUBEVENT_NOW_PLAYING_TRACK_INFO:
            printf("AVRCP Controller:     Track: %d\n", avrcp_subevent_now_playing_track_info_get_track(packet));
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_TOTAL_TRACKS_INFO:
            printf("AVRCP Controller:     Total Tracks: %d\n", avrcp_subevent_now_playing_total_tracks_info_get_total_tracks(packet));
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_TITLE_INFO:
            if (avrcp_subevent_now_playing_title_info_get_value_len(packet) > 0){
                memcpy(avrcp_subevent_value, avrcp_subevent_now_playing_title_info_get_value(packet), avrcp_subevent_now_playing_title_info_get_value_len(packet));
                printf("AVRCP Controller:     Title: %s\n", avrcp_subevent_value);
            }  
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_ARTIST_INFO:
            if (avrcp_subevent_now_playing_artist_info_get_value_len(packet) > 0){
                memcpy(avrcp_subevent_value, avrcp_subevent_now_playing_artist_info_get_value(packet), avrcp_subevent_now_playing_artist_info_get_value_len(packet));
                printf("AVRCP Controller:     Artist: %s\n", avrcp_subevent_value);
            }  
            break;
        
        case AVRCP_SUBEVENT_NOW_PLAYING_ALBUM_INFO:
            if (avrcp_subevent_now_playing_album_info_get_value_len(packet) > 0){
                memcpy(avrcp_subevent_value, avrcp_subevent_now_playing_album_info_get_value(packet), avrcp_subevent_now_playing_album_info_get_value_len(packet));
                printf("AVRCP Controller:     Album: %s\n", avrcp_subevent_value);
            }  
            break;
        
        case AVRCP_SUBEVENT_NOW_PLAYING_GENRE_INFO:
            if (avrcp_subevent_now_playing_genre_info_get_value_len(packet) > 0){
                memcpy(avrcp_subevent_value, avrcp_subevent_now_playing_genre_info_get_value(packet), avrcp_subevent_now_playing_genre_info_get_value_len(packet));
                printf("AVRCP Controller:     Genre: %s\n", avrcp_subevent_value);
            }  
            break;
        
        case AVRCP_SUBEVENT_PLAY_STATUS:
            printf("AVRCP Controller: Song length %"PRIu32" ms, Song position %"PRIu32" ms, Play status %s\n", 
                avrcp_subevent_play_status_get_song_length(packet), 
                avrcp_subevent_play_status_get_song_position(packet),
                avrcp_play_status2str(avrcp_subevent_play_status_get_play_status(packet)));
            break;
        
        case AVRCP_SUBEVENT_OPERATION_COMPLETE:
            printf("AVRCP Controller: %s complete\n", avrcp_operation2str(avrcp_subevent_operation_complete_get_operation_id(packet)));
            break;
        
        case AVRCP_SUBEVENT_OPERATION_START:
            printf("AVRCP Controller: %s start\n", avrcp_operation2str(avrcp_subevent_operation_start_get_operation_id(packet)));
            break;
       
        case AVRCP_SUBEVENT_NOTIFICATION_EVENT_TRACK_REACHED_END:
            printf("AVRCP Controller: Track reached end\n");
            break;

        case AVRCP_SUBEVENT_PLAYER_APPLICATION_VALUE_RESPONSE:
            printf("A2DP  Sink      : Set Player App Value %s\n", avrcp_ctype2str(avrcp_subevent_player_application_value_response_get_command_type(packet)));
            break;
            
       
        default:
            printf("AVRCP Controller: Event 0x%02x is not parsed\n", packet[2]);
            break;
    }  
}

static void avrcp_volume_changed(uint8_t volume){
    const btstack_audio_sink_t * audio = btstack_audio_sink_get_instance();
    if (audio){
        audio->set_volume(volume);
    }
}

static void avrcp_target_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    
    uint8_t volume;

    switch (packet[2]){
        case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
            volume = avrcp_subevent_notification_volume_changed_get_absolute_volume(packet);
            volume_percentage = volume * 100 / 127;
            printf("AVRCP Target    : Volume set to %d%% (%d)\n", volume_percentage, volume);
            avrcp_volume_changed(volume);
            break;
        
        case AVRCP_SUBEVENT_EVENT_IDS_QUERY:
#ifdef SUPPORT_VOLUME_CHANGE_NOTIFICATION
            avrcp_target_supported_events(avrcp_cid, events_num, events, sizeof(events));
#else
            avrcp_target_supported_events(avrcp_cid, 0, NULL, 0);
#endif
            break;
        case AVRCP_SUBEVENT_COMPANY_IDS_QUERY:
            avrcp_target_supported_companies(avrcp_cid, companies_num, companies, sizeof(companies));
            break;
        case AVRCP_SUBEVENT_OPERATION:{
            avrcp_operation_id_t operation_id = avrcp_subevent_operation_get_operation_id(packet);
            switch (operation_id){
                case AVRCP_OPERATION_ID_PLAY:
                    printf("AVRCP Target    : PLAY\n");
                    break;
                case AVRCP_OPERATION_ID_PAUSE:
                    printf("AVRCP Target    : PAUSE\n");
                    break;
                case AVRCP_OPERATION_ID_STOP:
                    printf("AVRCP Target    : STOP\n");
                    break;
                case AVRCP_OPERATION_ID_REWIND:
                    printf("AVRCP Target    : REWIND\n");
                    break;
                case AVRCP_OPERATION_ID_FAST_FORWARD:
                    printf("AVRCP Target    : FAST_FORWARD\n");
                    break;
                case AVRCP_OPERATION_ID_FORWARD:
                    printf("AVRCP Target    : FORWARD\n");
                    break;
                case AVRCP_OPERATION_ID_BACKWARD:
                    printf("AVRCP Target    : BACKWARD\n");
                    break;
                case AVRCP_OPERATION_ID_SKIP:
                    printf("AVRCP Target    : SKIP\n");
                    break;
                case AVRCP_OPERATION_ID_MUTE:
                    printf("AVRCP Target    : MUTE\n");
                    break;
                case AVRCP_OPERATION_ID_CHANNEL_UP:
                    printf("AVRCP Target    : CHANNEL_UP\n");
                    break;
                case AVRCP_OPERATION_ID_CHANNEL_DOWN:
                    printf("AVRCP Target    : CHANNEL_DOWN\n");
                    break;
                case AVRCP_OPERATION_ID_SELECT:
                    printf("AVRCP Target    : SELECT\n");
                    break;
                case AVRCP_OPERATION_ID_UP:
                    printf("AVRCP Target    : UP\n");
                    break;
                case AVRCP_OPERATION_ID_DOWN:
                    printf("AVRCP Target    : DOWN\n");
                    break;
                case AVRCP_OPERATION_ID_LEFT:
                    printf("AVRCP Target    : LEFT\n");
                    break;
                case AVRCP_OPERATION_ID_RIGHT:
                    printf("AVRCP Target    : RIGTH\n");
                    break;
                case AVRCP_OPERATION_ID_ROOT_MENU:
                    printf("AVRCP Target    : ROOT_MENU\n");
                    break;
                default:
                    return;
            }
            break;
        }
        default:
            printf("AVRCP Target    : Event 0x%02x is not parsed\n", packet[2]);
            break;
    }
}

static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) == HCI_EVENT_PIN_CODE_REQUEST) {
        bd_addr_t address;
        printf("Pin code request - using '0000'\n");
        hci_event_pin_code_request_get_bd_addr(packet, address);
        gap_pin_code_response(address, "0000");
    }
}
