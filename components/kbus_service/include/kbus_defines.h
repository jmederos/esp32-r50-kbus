#ifndef KBUS_DEFINES_H
#define KBUS_DEFINES_H

/**
 ** kbus device definitions from
 ** http://web.archive.org/web/20110318185825/http://ibus.stuge.se/IBus_Devices
 */
#define GM      0x00    //Body Module
#define CDC     0x18    //CD Changer
#define FUH     0x28    //Radio controlled clock
#define CCM     0x30    //Check control module
#define GT      0x3B    //Graphics driver (in navigation system)
#define DIA     0x3F    //Diagnostic
#define FBZV    0x40    //Remote control central locking
#define GTF     0x43    //Graphics driver for rear screen (in navigation system)
#define EWS     0x44    //Immobiliser
#define CID     0x46    //Central information display (flip-up LCD screen)
#define MFL     0x50    //Multi function steering wheel
#define MM_0    0x51    //Mirror memory
#define IHK     0x5B    //Integrated heating and air conditioning
#define PDC     0x60    //Park distance control
#define ONL     0x67    //unknown
#define RAD     0x68    //Radio
#define DSP     0x6A    //Digital signal processing audio amplifier
#define SM_0    0x72    //Seat memory
#define CDCD    0x76    //CD changer, DIN size.
#define NAVE    0x7F    //Navigation (Europe)
#define IKE     0x80    //Instrument cluster electronics
#define MM_1    0x9B    //Mirror memory
#define MM_2    0x9C    //Mirror memory
#define FMID    0xA0    //Rear multi-info-display
#define ABM     0xA4    //Air bag module
#define KAM     0xA8    //unknown
#define ASP     0xAC    //unknown
#define SES     0xB0    //Speed recognition system
#define NAVJ    0xBB    //Navigation (Japan)
#define GLO     0xBF    //Global, broadcast address
#define MID     0xC0    //Multi-info display
#define TEL     0xC8    //Telephone
#define LCM     0xD0    //Light control module
#define SM_1    0xDA    //Seat memory
#define GTHL    0xDA    //unknown
#define IRIS    0xE0    //Integrated radio information system
#define ANZV    0xE7    //Front display
#define RLS     0xE8    //Rain/Light Sensor
#define TV      0xED    //Television
#define BMBT    0xF0    //On-board monitor operating part
#define CSU     0xF5    //unknown
#define LOC     0xFF    //Local

/** 
 ** kbus command definitions from
 * http://web.archive.org/web/20110318185808/http://ibus.stuge.se/IBus_Messages
 ** AND
 * Testing w/NavCoder
 */
#define DEV_STAT_REQ        0x01    //Device status request
#define DEV_STAT_RDY        0x02    //Device status ready
#define BUS_STAT_REQ        0x03    //"Bus status request"
#define BUS_STAT_RPLY       0x04    //"Bus status"
#define DIAG_READ_MEM       0x06    //"DIAG read memory"
#define DIAG_WRTE_MEM_1     0x07    //"DIAG write memory"
#define DIAG_READ_CODING    0x08    //"DIAG read coding data"
#define DIAG_WRTE_MEM_2     0x09    //"DIAG write coding data"
#define VEHICLE_CTRL        0x0C    //Vehicle control

#define IGN_STAT_REQ        0x10    //"Ignition status request"
#define IGN_STAT_RPLY       0x11    //Ignition status
#define IKE_SENS_STAT_REQ   0x12    //"IKE sensor status request"
#define IKE_SENS_STAT_RPLY  0x13    //"IKE sensor status"
#define CTRY_CODE_STAT_REQ  0x14    //"Country coding status request"
#define CTRY_CODE_STAT_RPLY 0x15    //Country coding status
#define ODMTR_STAT_REQ      0x16    //"Odometer request"
#define ODMTR_STAT_RPLY     0x17    //"Odometer"
#define SPEED_RPM_REQ       0x18    //Speed/RPM
#define TEMP                0x19    //Temperature
#define IKE_TXT_GONG        0x1A    //"IKE text display/Gong"
#define IKE_TXT_STAT        0x1B    //"IKE text status"
#define GONG                0x1C    //"Gong"
#define TEMP_REQ            0x1D    //Temperature request
#define UTC_DATE_TIME       0x1F    //UTC time and date
#define DISPLAY_STATUS      0x20    //Display Status
#define MENU_TXT            0x21    //Menu Text
#define TXT_DISPLAY_CONF    0x22    //Text display confirmation
#define UPDATE_MID          0x23    //Update MID
#define UPDATE_ANZV         0x24    //Update ANZV
#define OBC_UPDATE          0x2A    //On-Board Computer State Update
#define TEL_LEDS            0x2B    //Telephone LED Indicators
#define TEL_STATUS          0x2C    //Telephone status
#define DSP_EQ_BUTT         0x34    //DSP Equalizer Button
#define CD_CTRL_REQ         0x38    //CD Control Message
#define CD_STAT_RPLY        0x39    //CD status
#define OBC_SET_DATA        0x40    //Set On-Board Computer Data
#define OBC_DATA_REQ        0x41    //On-Board Computer Data Request
#define BMBT_BUTT_1         0x48    //BMBT buttons
#define BMBT_BUTT_2         0x49    //BMBT buttons
#define TV_RGB_CTL          0x4F    //RGB Control
#define LAMP_STAT_REQ       0x5A    //Lamp state request
#define LAMP_STAT_RPLY      0x5B    //Lamp state
#define VEHICLE_STAT_REQ    0x53    //Vehicle data request
#define VEHICLE_STAT_RPLY   0x54    //Vehicle data status
#define LAMP_STATUS         0x5B    //Lamp Status
#define RIP_STAT_REQ        0x71    //Rain sensor status request
#define DIAG_DATA           0xA0    //"DIAG data"
#define NAV_CTL             0xAA    //Navigation Control

#endif //KBUS_DEFINES_H