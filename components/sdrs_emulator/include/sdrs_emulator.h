#ifndef SDRS_EMULATOR_H
#define SDRS_EMULATOR_H

// SDRS Common Subcommands
#define SDRS_POWER_MODE     0x00
#define SDRS_HEARTBEAT      0x02

// SDRS Request Subcommands
#define SDRS_REQ_SLEEP      0x01
#define SDRS_REQ_CHAN_UP    0x03
#define SDRS_REQ_CHAN_DN    0x04
#define SDRS_REQ_PRESET     0x08

#define SDRS_REQ_ESN        0x14
#define SDRS_REQ_BANK_UP    0x15

#define SDRS_REQ_ARTIST     0x0E
#define SDRS_REQ_SONG       0x0F

// SDRS Reply Subcommands
#define SDRS_UPDATE_TXT     0x01
#define SDRS_CHAN_DN_ACK    0x03

typedef struct {
    char chan_disp[256];
    char song_disp[128];
    char artist_disp[64];
    char esn_disp[32];
} sdrs_display_buf_t;

void sdrs_init_emulation(QueueHandle_t kbus_tx_queue, sdrs_display_buf_t* display_buffer);
uint8_t sdrs_enqueue_msg(void* message, TickType_t ticksToWait);
#endif //SDRS_EMULATOR_H
