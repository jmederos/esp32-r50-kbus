/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef BLE_ANCS_H
#define BLE_ANCS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/ble_hs.h"

//EventID values
typedef enum {
   EventIDNotificationAdded     = 0,
   EventIDNotificationModified  = 1,
   EventIDNotificationRemoved   = 2,
   //Reserved EventID values = 3–255
} ble_ancs_EventID;

//EventFlags
typedef enum {
   EventFlagSilent              = (1 << 0),
   EventFlagImportant           = (1 << 1),
   EventFlagPreExisting         = (1 << 2),
   EventFlagPositiveAction      = (1 << 3),
   EventFlagNegativeAction      = (1 << 4),
   //Reserved EventFlags   = (1 << 5)–(1 << 7)
}ble_ancs_EventFlags;

// CategoryID values
typedef enum {
   CategoryIDOther              = 0,
   CategoryIDIncomingCall       = 1,
   CategoryIDMissedCall         = 2,
   CategoryIDVoicemail          = 3,
   CategoryIDSocial             = 4,
   CategoryIDSchedule           = 5,
   CategoryIDEmail              = 6,
   CategoryIDNews               = 7,
   CategoryIDHealthAndFitness   = 8,
   CategoryIDBusinessAndFinance = 9,
   CategoryIDLocation           = 10,
   CategoryIDEntertainment      = 11,
   //Reserved CategoryID values   = 12–255
} ble_ancs_CategoryID;

//CommandID values
typedef enum {
   CommandIDGetNotificationAttributes = 0,
   CommandIDGetAppAttributes          = 1,
   CommandIDPerformNotificationAction = 2,
   //Reserved CommandID values = 3–255
} ble_ancs_CommandID;

//NotificationAttributeID
typedef enum {
   NotificationAttributeIDAppIdentifier       = 0,
   NotificationAttributeIDTitle               = 1, //(Needs to be followed by a 2-bytes max length parameter)
   NotificationAttributeIDSubtitle            = 2, //(Needs to be followed by a 2-bytes max length parameter)
   NotificationAttributeIDMessage             = 3, //(Needs to be followed by a 2-bytes max length parameter)
   NotificationAttributeIDMessageSize         = 4,
   NotificationAttributeIDDate                = 5,
   NotificationAttributeIDPositiveActionLabel = 6,
   NotificationAttributeIDNegativeActionLabel = 7,
   //Reserved NotificationAttributeID values = 8–255
} ble_ancs_NotificationAttributeID;

/*
Note: The format of the NotificationAttributeIDMessageSize constant is a string that represents the integral value
of the message size. The format of the NotificationAttributeIDDate constant is a string that uses the Unicode Technical
Standard (UTS) #35 date format pattern yyyyMMdd'T'HHmmSS. The format of all the other constants in Table 3-5 are UTF-8
strings.
*/

//ActionID values
typedef enum {
   ActionIDPositive = 0,
   ActionIDNegative = 1,
   //Reserved ActionID values = 2–255
} ble_ActionID;

//AppAttributeID Values
typedef enum {
   AppAttributeIDDisplayName = 0,
   //Reserved AppAttributeID values = 1–255
} ble_AppAttributeID;

typedef struct {
    uint8_t                            noti_attribute_id;
    uint16_t                           attribute_len;
} ble_noti_attr_list_t;

typedef enum {
   Unknown_command   = (0xA0), //The commandID was not recognized by the NP.
   Invalid_command   = (0xA1), //The command was improperly formatted.
   Invalid_parameter = (0xA2), // One of the parameters (for example, the NotificationUID) does not refer to an existing object on the NP.
   Action_failed     = (0xA3), //The action was not performed
} ble_ancs_error_code;

typedef enum {
   attr_appidentifier_index   = 0, //The commandID was not recognized by the NP.
   attr_title_index,
   attr_subtitle_index,
   attr_message_index,
   attr_messagesize_index,
   attr_date_index,
   attr_positiveactionlabel_index,
   attr_negativeactionlabel_index,
} ble_ancs_attr_index;

#define ESP_NOTIFICATIONUID_LEN   4

// ANCS UUID: 7905F431-B5CE-4E99-A40F-4B1E122D00D0
static ble_uuid128_t apple_nc_service_uuid = BLE_UUID128_INIT(
            0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4, 0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79);

//In its basic form, the ANCS exposes three characteristics:
// Notification Source UUID: 9FBF120D-6301-42D9-8C58-25E699A21DBD(notifiable)
static ble_uuid128_t ancs_notification_src_uuid = BLE_UUID128_INIT(
        0xbd, 0x1d, 0xa2, 0x99, 0xe6, 0x25, 0x58, 0x8c, 0xd9, 0x42, 0x01, 0x63, 0x0d, 0x12, 0xbf, 0x9f);
// Control Point UUID:69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9(writeable with response)
static ble_uuid128_t ancs_control_point_uuid = BLE_UUID128_INIT(
        0xd9, 0xd9, 0xaa, 0xfd, 0xbd, 0x9b, 0x21, 0x98, 0xa8, 0x49, 0xe1, 0x45, 0xf3, 0xd8, 0xd1, 0x69);
// Data Source UUID:22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB(notifiable)
static ble_uuid128_t ancs_data_source_uuid = BLE_UUID128_INIT(
        0xfb, 0x7b, 0x7c, 0xce, 0x6a, 0xb3, 0x44, 0xbe, 0xb5, 0x4b, 0xd6, 0x24, 0xe9, 0xc6, 0xea, 0x22);

static ble_noti_attr_list_t p_attr[8] = {
    [attr_appidentifier_index] = {
        .noti_attribute_id = NotificationAttributeIDAppIdentifier,
        .attribute_len = 0,
    },
    [attr_title_index] = {
        .noti_attribute_id = NotificationAttributeIDTitle,
        .attribute_len = 0xFFFF,
    },
    [attr_subtitle_index] = {
        .noti_attribute_id = NotificationAttributeIDSubtitle,
        .attribute_len = 0xFFFF,
    },
    [attr_message_index] = {
        .noti_attribute_id = NotificationAttributeIDMessage,
        .attribute_len = 0xFFFF,
    },
    [attr_messagesize_index] = {
        .noti_attribute_id = NotificationAttributeIDMessageSize,
        .attribute_len = 0,
    },
    [attr_date_index] = {
        .noti_attribute_id = NotificationAttributeIDDate,
        .attribute_len = 0,
    },
    [attr_positiveactionlabel_index] = {
        .noti_attribute_id = NotificationAttributeIDPositiveActionLabel,
        .attribute_len = 0,
    },
    [attr_negativeactionlabel_index] = {
        .noti_attribute_id = NotificationAttributeIDNegativeActionLabel,
        .attribute_len = 0,
    },
};

// attribute handles 
extern uint16_t ancs_notification_src_handle;
extern uint16_t ancs_data_src_handle;
extern uint16_t ancs_ctrl_point_handle;
extern uint16_t shared_e_handle;
extern bool ancs_setup_complete;

// service discovery callback
int ancs_service_discovered_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *svc, void *arg);

// 
char *EventID_to_String(uint8_t EventID);
char *CategoryID_to_String(uint8_t CategoryID);

// decoding functions
void ancs_receive_apple_notification_source(uint8_t *message, uint16_t message_len);
void ancs_receive_apple_data_source(uint8_t *message, uint16_t message_len);
void ancs_get_notification_attributes(uint16_t conn_handle,
                                     uint16_t attr_handle,
                                     uint8_t *notificationUID,
                                     uint8_t num_attr,
                                     ble_noti_attr_list_t *p_attr);
void ancs_perform_notification_action(uint8_t *notificationUID,
                                     uint8_t ActionID,
                                     uint16_t conn_handle,
                                     uint16_t attr_handle);

#endif