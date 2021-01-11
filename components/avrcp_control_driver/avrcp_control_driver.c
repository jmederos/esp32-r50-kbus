/*
 * Copyright (C) 2020 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY MATTHIAS RINGWALD AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// esp-idf includes
#include "esp_system.h"
#include "esp_log.h"

#include "btstack.h"

#include "avrcp_control_driver.h"

static const char* TAG = "avrcp-ctl";

static TaskHandle_t bt_service_task;

static bd_addr_t device_addr;

static uint8_t  sdp_avrcp_controller_service_buffer[200];
static uint8_t  device_id_sdp_service_buffer[100];

static uint16_t avrcp_cid = 0;
static bool     avrcp_connected = false;
static uint8_t  avrcp_subevent_value[100];

// Now Playing Info
static char track_str[256];
static char artist_str[256];
static char album_str[256];
static uint32_t track_len_ms = 0;
static uint8_t track_no = 0;
static uint8_t total_tracks = 0;

/* Setup AVRCP service */
static void avrcp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void avrcp_controller_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

int avrcp_setup(char* announce_str) {
    return avrcp_setup_with_addr_and_notify(announce_str, "00:00:00:00:00:00", NULL);
}

int avrcp_setup_with_addr_and_notify(char* announce_str, char* cxn_address, TaskHandle_t service_task){
    bt_service_task = service_task;
    
    // Initialize AVRCP service
    avrcp_init();
    avrcp_register_packet_handler(&avrcp_packet_handler);
    
    // Initialize AVRCP Controller
    avrcp_controller_init();
    avrcp_controller_register_packet_handler(&avrcp_controller_packet_handler);

    // Create AVRCP Controller service record and register it with SDP
    memset(sdp_avrcp_controller_service_buffer, 0, sizeof(sdp_avrcp_controller_service_buffer));
    uint16_t controller_supported_features = AVRCP_FEATURE_MASK_CATEGORY_MONITOR_OR_AMPLIFIER | AVRCP_FEATURE_MASK_CATEGORY_PLAYER_OR_RECORDER;

#ifdef AVRCP_BROWSING_ENABLED
    controller_supported_features |= AVRCP_FEATURE_MASK_BROWSING;
#endif
    avrcp_controller_create_sdp_record(sdp_avrcp_controller_service_buffer, 0x10002, controller_supported_features, NULL, NULL);
    sdp_register_service(sdp_avrcp_controller_service_buffer);

    // Create Device ID (PnP) service record and register it with SDP
    memset(device_id_sdp_service_buffer, 0, sizeof(device_id_sdp_service_buffer));
    device_id_create_sdp_record(device_id_sdp_service_buffer, 0x10004, DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH, BLUETOOTH_COMPANY_ID_APPLE_INC, 1, 1);
    sdp_register_service(device_id_sdp_service_buffer);

    // Set local name with a template Bluetooth address, that will be automatically
    // replaced with a actual address once it is available, i.e. when BTstack boots
    // up and starts talking to a Bluetooth module.
    gap_set_local_name(announce_str);
    gap_discoverable_control(1);
    gap_set_class_of_device(0x240418);

    // Parse and store connection address
    sscanf_bd_addr(cxn_address, device_addr);

    // Set the avrcp_initialized bit
    if(bt_service_task != NULL) xTaskNotify(bt_service_task, 0x01, eSetBits);

    return 0;
}

uint8_t avrcp_ctl_connect() {
    if(avrcp_connected == true) return ERROR_CODE_SUCCESS;
    return avrcp_connect(device_addr, &avrcp_cid);
}

uint8_t avrcp_ctl_disconnect() {
    if(avrcp_connected == false) return ERROR_CODE_SUCCESS;
    return avrcp_disconnect(avrcp_cid);
}

uint8_t avrcp_ctl_play() {
    return avrcp_controller_play(avrcp_cid);
}

uint8_t avrcp_ctl_pause() {
    return avrcp_controller_pause(avrcp_cid);
}

uint8_t avrcp_ctl_stop() {
    return avrcp_controller_stop(avrcp_cid);
}

uint8_t avrcp_ctl_next() {
    return avrcp_controller_forward(avrcp_cid);
}

uint8_t avrcp_ctl_prev() {
    return avrcp_controller_backward(avrcp_cid);
}

uint8_t avrcp_ctl_start_ff() {
    return avrcp_controller_press_and_hold_fast_forward(avrcp_cid);
}

uint8_t avrcp_ctl_start_rwd() {
    return avrcp_controller_press_and_hold_rewind(avrcp_cid);
}

uint8_t avrcp_ctl_end_long_press() {
    return avrcp_controller_release_press_and_hold_cmd(avrcp_cid);
}

uint8_t avrcp_req_now_playing() {
    return avrcp_controller_get_now_playing_info(avrcp_cid);
}

char* avrcp_get_track_str() {
    return track_str;
}
char* avrcp_get_album_str() {
    return album_str;
}
char* avrcp_get_artist_str() {
    return artist_str;
}

uint16_t avrcp_get_track_info(){
    uint16_t track_info = 0x0000;
    track_info ^= track_no;
    track_info = track_info << 8;
    track_info ^= total_tracks;
    return track_info;
}
uint32_t avrcp_get_track_len_ms(){
    return track_len_ms;
}

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
                ESP_LOGW(TAG, "AVRCP: Connection failed: status 0x%02x", status);
                avrcp_cid = 0;
                // Notify service task that connection failed
                if(bt_service_task != NULL) xTaskNotify(bt_service_task, 0x01, eSetValueWithOverwrite);
                return;
            }

            avrcp_cid = local_cid;
            avrcp_connected = true;
            avrcp_subevent_connection_established_get_bd_addr(packet, adress);
            ESP_LOGI(TAG, "AVRCP: Connected to %s, cid 0x%02x", bd_addr_to_str(adress), avrcp_cid);

            // automatically enable notifications
            avrcp_controller_enable_notification(avrcp_cid, AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
            avrcp_controller_enable_notification(avrcp_cid, AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED);
            avrcp_controller_enable_notification(avrcp_cid, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);

            // Notify service task that connection succeeded
            if(bt_service_task != NULL) xTaskNotify(bt_service_task, 0x02, eSetBits);
            return;
        }
        
        case AVRCP_SUBEVENT_CONNECTION_RELEASED:
            ESP_LOGI(TAG, "AVRCP: Channel released: cid 0x%02x", avrcp_subevent_connection_released_get_avrcp_cid(packet));
            avrcp_cid = 0;
            avrcp_connected = false;
            // Notify service task that connection failed
            if(bt_service_task != NULL) xTaskNotify(bt_service_task, 0x01, eSetValueWithOverwrite);
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
                    ESP_LOGD(TAG, "AVRCP Controller: playback position changed, no track is selected");
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
            ESP_LOGD(TAG, "AVRCP Controller: Playback position changed, position %d ms", (unsigned int) avrcp_subevent_notification_playback_pos_changed_get_playback_position_ms(packet));
            break;
        case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_STATUS_CHANGED:
            ESP_LOGD(TAG, "AVRCP Controller: Playback status changed %s", avrcp_play_status2str(avrcp_subevent_notification_playback_status_changed_get_play_status(packet)));
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_NOW_PLAYING_CONTENT_CHANGED:
            ESP_LOGD(TAG, "AVRCP Controller: Playing content changed");
            avrcp_controller_get_now_playing_info(avrcp_cid);
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_TRACK_CHANGED:
            ESP_LOGD(TAG, "AVRCP Controller: Track changed");
            ESP_LOGD(TAG, "packet_type: 0x%02x\t\tchannel: %d\tsize: %d\tpacket_addr: %x", packet_type, channel, size, (int)packet);
            ESP_LOG_BUFFER_HEXDUMP(TAG, packet, 16, ESP_LOG_DEBUG);
            if(bt_service_task != NULL) xTaskNotify(bt_service_task, 0x04, eSetBits);
            // uint8_t got_cid = avrcp_subevent_notification_track_changed_get_avrcp_cid(packet);
            // uint8_t got_cmd_type = avrcp_subevent_notification_track_changed_get_command_type(packet);
            // ESP_LOGD(TAG, "Track Changed cid: 0x%02x  cmd_type: 0x%02x", got_cid, got_cmd_type);
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
            ESP_LOGD(TAG, "AVRCP Controller: Absolute volume changed %d", avrcp_subevent_notification_volume_changed_get_absolute_volume(packet));
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_AVAILABLE_PLAYERS_CHANGED:
            ESP_LOGD(TAG, "AVRCP Controller: Changed");
            return; 
        case AVRCP_SUBEVENT_SHUFFLE_AND_REPEAT_MODE:{
            uint8_t shuffle_mode = avrcp_subevent_shuffle_and_repeat_mode_get_shuffle_mode(packet);
            uint8_t repeat_mode  = avrcp_subevent_shuffle_and_repeat_mode_get_repeat_mode(packet);
            ESP_LOGD(TAG, "AVRCP Controller: %s, %s", avrcp_shuffle2str(shuffle_mode), avrcp_repeat2str(repeat_mode));
            break;
        }
        case AVRCP_SUBEVENT_NOW_PLAYING_TRACK_INFO:
            track_no = avrcp_subevent_now_playing_track_info_get_track(packet);
            ESP_LOGD(TAG, "AVRCP Controller:     Track: %d", track_no);
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_TOTAL_TRACKS_INFO:
            total_tracks = avrcp_subevent_now_playing_total_tracks_info_get_total_tracks(packet);
            ESP_LOGD(TAG, "AVRCP Controller:     Total Tracks: %d", total_tracks);
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_TITLE_INFO:
            if (avrcp_subevent_now_playing_title_info_get_value_len(packet) > 0){
                bzero(track_str, sizeof(track_str));
                memcpy(track_str, avrcp_subevent_now_playing_title_info_get_value(packet), avrcp_subevent_now_playing_title_info_get_value_len(packet));
                ESP_LOGD(TAG, "AVRCP Controller:     Title: %s", track_str);
            }  
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_ARTIST_INFO:
            if (avrcp_subevent_now_playing_artist_info_get_value_len(packet) > 0){
                bzero(artist_str, sizeof(artist_str));
                memcpy(artist_str, avrcp_subevent_now_playing_artist_info_get_value(packet), avrcp_subevent_now_playing_artist_info_get_value_len(packet));
                ESP_LOGD(TAG, "AVRCP Controller:     Artist: %s", artist_str);
            }  
            break;
        
        case AVRCP_SUBEVENT_NOW_PLAYING_ALBUM_INFO:
            if (avrcp_subevent_now_playing_album_info_get_value_len(packet) > 0){
                bzero(album_str, sizeof(album_str));
                memcpy(album_str, avrcp_subevent_now_playing_album_info_get_value(packet), avrcp_subevent_now_playing_album_info_get_value_len(packet));
                ESP_LOGD(TAG, "AVRCP Controller:     Album: %s", album_str);
            }  
            break;
        
        case AVRCP_SUBEVENT_NOW_PLAYING_GENRE_INFO:
            if (avrcp_subevent_now_playing_genre_info_get_value_len(packet) > 0){
                memcpy(avrcp_subevent_value, avrcp_subevent_now_playing_genre_info_get_value(packet), avrcp_subevent_now_playing_genre_info_get_value_len(packet));
                ESP_LOGD(TAG, "AVRCP Controller:     Genre: %s", avrcp_subevent_value);
            }  
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_SONG_LENGTH_MS_INFO:
            track_len_ms = avrcp_subevent_now_playing_song_length_ms_info_get_song_length(packet);
            ESP_LOGD(TAG, "AVRCP Controller:     Length: %"PRIu32" ms", track_len_ms);
            // In testing, this is consistently the last packet of info parsed, so let's notify bt_task to pull new data.
            if(bt_service_task != NULL) xTaskNotify(bt_service_task, 0x08, eSetBits);
            break;
        
        case AVRCP_SUBEVENT_PLAY_STATUS:
            track_len_ms = avrcp_subevent_play_status_get_song_length(packet);
            ESP_LOGD(TAG, "AVRCP Controller: Song length %"PRIu32" ms, Song position %"PRIu32" ms, Play status %s", 
                track_len_ms, 
                avrcp_subevent_play_status_get_song_position(packet),
                avrcp_play_status2str(avrcp_subevent_play_status_get_play_status(packet)));
            break;
        
        case AVRCP_SUBEVENT_OPERATION_COMPLETE:
            ESP_LOGD(TAG, "AVRCP Controller: %s complete", avrcp_operation2str(avrcp_subevent_operation_complete_get_operation_id(packet)));
            break;
        
        case AVRCP_SUBEVENT_OPERATION_START:
            ESP_LOGD(TAG, "AVRCP Controller: %s start", avrcp_operation2str(avrcp_subevent_operation_start_get_operation_id(packet)));
            break;
       
        case AVRCP_SUBEVENT_NOTIFICATION_EVENT_TRACK_REACHED_START:
            ESP_LOGD(TAG, "AVRCP Controller: Track reached start");
            break;

        case AVRCP_SUBEVENT_NOTIFICATION_EVENT_TRACK_REACHED_END:
            ESP_LOGD(TAG, "AVRCP Controller: Track reached end");
            break;

        case AVRCP_SUBEVENT_PLAYER_APPLICATION_VALUE_RESPONSE:
            ESP_LOGD(TAG, "A2DP  Sink      : Set Player App Value %s", avrcp_ctype2str(avrcp_subevent_player_application_value_response_get_command_type(packet)));
            break;

        default:
            ESP_LOGD(TAG, "AVRCP Controller: Event 0x%02x is not parsed", packet[2]);
            break;
    }
}
