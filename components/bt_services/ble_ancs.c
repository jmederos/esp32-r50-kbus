/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "ble_ancs.h"

#define NimBLE_ANCS_TAG  "NimBLE_ANCS"

uint16_t ancs_notification_src_handle;
uint16_t ancs_data_src_handle;
uint16_t ancs_ctrl_point_handle;

/*
| EventID(1 Byte) | EventFlags(1 Byte) | CategoryID(1 Byte) | CategoryCount(1 Byte) | NotificationUID(4 Bytes) |

A GATT notification delivered through the Notification Source characteristic contains the following information:
* EventID: This field informs the accessory whether the given iOS notification was added, modified, or removed. The enumerated values for this field are defined
            in EventID Values.
* EventFlags: A bitmask whose set bits inform an NC of specificities with the iOS notification. For example, if an iOS notification is considered “important”,
              the NC may want to display a more aggressive user interface (UI) to make sure the user is properly alerted. The enumerated bits for this field
              are defined in EventFlags.
* CategoryID: A numerical value providing a category in which the iOS notification can be classified. The NP will make a best effort to provide an accurate category
              for each iOS notification. The enumerated values for this field are defined in CategoryID Values.
* CategoryCount: The current number of active iOS notifications in the given category. For example, if two unread emails are sitting in a user’s email inbox, and a new
                 email is pushed to the user’s iOS device, the value of CategoryCount is 3.
* NotificationUID: A 32-bit numerical value that is the unique identifier (UID) for the iOS notification. This value can be used as a handle in commands sent to the
                   Control Point characteristic to interact with the iOS notification.
*/

char *EventID_to_String(uint8_t EventID)
{
    char *str = NULL;
    switch (EventID)
    {
        case EventIDNotificationAdded:
            str = "New message";
            break;
        case EventIDNotificationModified:
            str = "Modified message";
            break;
        case EventIDNotificationRemoved:
            str = "Removed message";
            break;
        default:
            str = "unknown EventID";
            break;
    }
    return str;
}

char *CategoryID_to_String(uint8_t CategoryID)
{
    char *Cidstr = NULL;
    switch(CategoryID) {
        case CategoryIDOther:
            Cidstr = "Other";
            break;
        case CategoryIDIncomingCall:
            Cidstr = "IncomingCall";
            break;
        case CategoryIDMissedCall:
            Cidstr = "MissedCall";
            break;
        case CategoryIDVoicemail:
            Cidstr = "Voicemail";
            break;
        case CategoryIDSocial:
            Cidstr = "Social";
            break;
        case CategoryIDSchedule:
            Cidstr = "Schedule";
            break;
        case CategoryIDEmail:
            Cidstr = "Email";
            break;
        case CategoryIDNews:
            Cidstr = "News";
            break;
        case CategoryIDHealthAndFitness:
            Cidstr = "HealthAndFitness";
            break;
        case CategoryIDBusinessAndFinance:
            Cidstr = "BusinessAndFinance";
            break;
        case CategoryIDLocation:
            Cidstr = "Location";
            break;
        case CategoryIDEntertainment:
            Cidstr = "Entertainment";
            break;
        default:
            Cidstr = "Unknown CategoryID";
            break;
    }
    return Cidstr;
}

static char *Errcode_to_String(uint16_t status)
{
    char *Errstr = NULL;
    switch (status) {
        case Unknown_command:
            Errstr = "Unknown_command";
            break;
        case Invalid_command:
            Errstr = "Invalid_command";
            break;
        case Invalid_parameter:
            Errstr = "Invalid_parameter";
            break;
        case Action_failed:
            Errstr = "Action_failed";
            break;
        default:
            Errstr = "unknown_failed";
            break;
    }
    return Errstr;

}

/*
| EventID(1 Byte) | EventFlags(1 Byte) | CategoryID(1 Byte) | CategoryCount(1 Byte) | NotificationUID(4 Bytes) |
*/

void ancs_receive_apple_notification_source(uint8_t *message, uint16_t message_len)
{
    if (!message || message_len < 5) {
        return;
    }

    uint8_t EventID    = message[0];
    char    *EventIDS  = EventID_to_String(EventID);
    uint8_t EventFlags = message[1];
    uint8_t CategoryID = message[2];
    char    *Cidstr    = CategoryID_to_String(CategoryID);
    uint8_t CategoryCount = message[3];
    uint32_t NotificationUID = (message[4]) | (message[5]<< 8) | (message[6]<< 16) | (message[7] << 24);
    ESP_LOGI(NimBLE_ANCS_TAG, "EventID:%s EventFlags:0x%x CategoryID:%s CategoryCount:%d NotificationUID:%" PRIu32, EventIDS, EventFlags, Cidstr, CategoryCount, NotificationUID);
}

void ancs_receive_apple_data_source(uint8_t *message, uint16_t message_len)
{
    if (!message || message_len == 0) {
        return;
    }
    uint8_t Command_id = message[0];
    switch (Command_id)
    {
        case CommandIDGetNotificationAttributes: {
            uint32_t NotificationUID = (message[1]) | (message[2]<< 8) | (message[3]<< 16) | (message[4] << 24);
            uint32_t remian_attr_len = message_len - 5;
            uint8_t *attrs = &message[5];
            ESP_LOGI(NimBLE_ANCS_TAG, "recevice Notification Attributes response Command_id %d NotificationUID %" PRIu32, Command_id, NotificationUID);
            while(remian_attr_len > 0) {
                uint8_t AttributeID = attrs[0];
                uint16_t len = attrs[1] | (attrs[2] << 8);
                if(len > (remian_attr_len -3)) {
                    ESP_LOGE(NimBLE_ANCS_TAG, "data error");
                    break;
                }
                switch (AttributeID)
                {
                    case NotificationAttributeIDAppIdentifier:
                        ESP_LOG_BUFFER_CHAR("Identifier", &attrs[3], len);
                        break;
                    case NotificationAttributeIDTitle:
                        ESP_LOG_BUFFER_CHAR("Title", &attrs[3], len);
                        break;
                    case NotificationAttributeIDSubtitle:
                        ESP_LOG_BUFFER_CHAR("Subtitle", &attrs[3], len);
                        break;
                    case NotificationAttributeIDMessage:
                        ESP_LOG_BUFFER_CHAR("Message", &attrs[3], len);
                        break;
                    case NotificationAttributeIDMessageSize:
                        ESP_LOG_BUFFER_CHAR("MessageSize", &attrs[3], len);
                        break;
                    case NotificationAttributeIDDate:
                        //yyyyMMdd'T'HHmmSS
                        ESP_LOG_BUFFER_CHAR("Date", &attrs[3], len);
                        break;
                    case NotificationAttributeIDPositiveActionLabel:
                        ESP_LOG_BUFFER_HEX("PActionLabel", &attrs[3], len);
                        break;
                    case NotificationAttributeIDNegativeActionLabel:
                        ESP_LOG_BUFFER_HEX("NActionLabel", &attrs[3], len);
                        break;
                    default:
                        ESP_LOG_BUFFER_HEX("unknownAttributeID", &attrs[3], len);
                        break;
                }

                attrs += (1 + 2 + len);
                remian_attr_len -= (1 + 2 + len);
            }

            break;
        }
        case CommandIDGetAppAttributes:
            ESP_LOGI(NimBLE_ANCS_TAG, "recevice APP Attributes response");
            break;
        case CommandIDPerformNotificationAction:
            ESP_LOGI(NimBLE_ANCS_TAG, "recevice Perform Notification Action");
            break;
        default:
            ESP_LOGI(NimBLE_ANCS_TAG, "unknown Command ID");
            break;
    }
}


static int ancs_on_subscribe(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr,
                        void *arg)
{
    MODLOG_DFLT(INFO,
                "Subscribe to the custom subscribable characteristic complete; "
                "status=%d conn_handle=%d",
                error->status, conn_handle);

    ancs_setup_complete = true;
    return 0;
}

static int ancs_dsc_discovered_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                                uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                                void *arg)
{
    int rc = 0;
    uint8_t cccd_val[2] = {0x01, 0x00}; // Enable notification
    if (error->status == 0) {
        if (ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            rc = ble_gattc_write_flat(conn_handle,
                                    dsc->handle,
                                    cccd_val, sizeof(cccd_val),
                                    ancs_on_subscribe, NULL);

            if (rc != 0) {
                MODLOG_DFLT(WARN, "Failed to subscribe to (rc=%d)", rc);
                return rc;
            }
        }
    } else {
        rc = error->status;
        MODLOG_DFLT(ERROR, "ancs_dsc_cb ERROR: 0x%04x %s", rc, Errcode_to_String(rc));
    }

    return rc;
}

static int ancs_chr_discovered_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                    const struct ble_gatt_chr *chr, void *arg)
{
    int rc = 0;
    if (error->status == 0) {

        MODLOG_DFLT(INFO, "ancs_chr_uuid: %x \tprop: %08x", chr->uuid.u32, chr->properties);

        /* Notification Source */
        if ((chr->properties & BLE_GATT_CHR_PROP_NOTIFY) &&
             ble_uuid_cmp(&chr->uuid.u, &ancs_notification_src_uuid.u) == 0) {

            ancs_notification_src_handle = chr->val_handle;
            rc = ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, shared_e_handle, ancs_dsc_discovered_cb, NULL);
            MODLOG_DFLT(INFO, "Found Notification Source characnteristic");

        }
        /* Data Source */
        else if ((chr->properties & BLE_GATT_CHR_PROP_NOTIFY) &&
                 ble_uuid_cmp(&chr->uuid.u, &ancs_data_source_uuid.u) == 0) {

            ancs_data_src_handle = chr->val_handle;
            rc = ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, shared_e_handle, ancs_dsc_discovered_cb, NULL);
            MODLOG_DFLT(INFO, "Found Data Source characteristic");

        }
        /* Control Point */
        else if ((chr->properties & BLE_GATT_CHR_PROP_WRITE) &&
                 ble_uuid_cmp(&chr->uuid.u, &ancs_control_point_uuid.u) == 0) {

            ancs_ctrl_point_handle = chr->val_handle;
            MODLOG_DFLT(INFO, "Found Control Point characteristic");
        }
    } else{
        rc = error->status;
        MODLOG_DFLT(ERROR, "ancs_chr_cb ERROR: 0x%04x %s", rc, Errcode_to_String(rc));
    }

    return rc;
}

int ancs_service_discovered_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                                    const struct ble_gatt_svc *svc, void *arg)
{
    int rc;
    if (error->status == 0) {
        MODLOG_DFLT(WARN, "svc_uuid: %x", svc->uuid.u32);
        MODLOG_DFLT(INFO, "svc_start_handle: %08x svc_end_handle: %08x", svc->start_handle, svc->end_handle);
        
        if (svc == NULL) {
            MODLOG_DFLT(ERROR, "ANCS service not found");
            return -1;
        }

        shared_e_handle = svc->end_handle;
        /* discover characteristics within this service range */
        rc = ble_gattc_disc_all_chrs(conn_handle, svc->start_handle, svc->end_handle, ancs_chr_discovered_cb, NULL);
    } else {
        rc = error->status;
        MODLOG_DFLT(ERROR, "ancs_service_cb ERROR: 0x%04x %s", rc, Errcode_to_String(rc));
    }
    return rc;
}

void ancs_get_notification_attributes(uint16_t conn_handle,
                                     uint16_t attr_handle,
                                     uint8_t *notificationUID,
                                     uint8_t num_attr,
                                     ble_noti_attr_list_t *p_attr)
{
    uint8_t cmd[600] = {0};
    uint32_t index = 0;

    cmd[index++] = CommandIDGetNotificationAttributes;

    memcpy(&cmd[index], notificationUID, ESP_NOTIFICATIONUID_LEN);
    index += ESP_NOTIFICATIONUID_LEN;

    while (num_attr > 0) {
        cmd[index++] = p_attr->noti_attribute_id;

        if (p_attr->attribute_len > 0) {
            cmd[index++] = (uint8_t)(p_attr->attribute_len & 0xFF);
            cmd[index++] = (uint8_t)((p_attr->attribute_len >> 8) & 0xFF);
        }

        p_attr++;
        num_attr--;
    }

    int rc = ble_gattc_write_flat(conn_handle,
                                  attr_handle,
                                  cmd,
                                  index,
                                  NULL,  // no callback
                                  NULL); // no argument

    if (rc != 0) {
        MODLOG_DFLT(WARN, "Failed to write notification attribute request: rc=%d\n", rc);

    }
}

void ancs_perform_notification_action(uint8_t *notificationUID,
                                     uint8_t ActionID,
                                     uint16_t conn_handle,
                                     uint16_t attr_handle)
{
    uint8_t buffer[600] = {0};

    uint16_t index = 0;
    buffer[index++] = CommandIDPerformNotificationAction;  // define this as needed

    memcpy(&buffer[index], notificationUID, ESP_NOTIFICATIONUID_LEN);
    index += ESP_NOTIFICATIONUID_LEN;

    buffer[index++] = ActionID;

    int rc = ble_gattc_write_flat(conn_handle,
                                  attr_handle,
                                  buffer,
                                  index,
                                  NULL, // no callback
                                  NULL); // no callback argument

    if (rc != 0) {
        MODLOG_DFLT(WARN, "Failed to write perform action command: rc=%d\n", rc);
    }
}
