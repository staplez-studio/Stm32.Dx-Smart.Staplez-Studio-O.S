#ifndef SD_BRIDGE_H
#define SD_BRIDGE_H
/* Browse the micro SD card of the ESP32-CAM. The ESP32-CAM is on the same UART as the Wi-Fi commands
 * (PA2/PA3); it must run esp32cam_sd_bridge.ino (or any firmware that answers the AT+SD... commands in
 * sd_proto.h). Names only: nothing here reads file contents. */
#include "sd_proto.h"

typedef enum {
    SD_OK = 0,
    SD_NO_LINK,        /* the ESP32-CAM does not answer at all: wires, power, baud */
    SD_NO_BRIDGE,      /* it answers AT but does not know AT+SDINFO: wrong firmware */
    SD_NO_CARD,        /* bridge present, no card inserted / not mountable */
    SD_BAD_PATH,       /* folder missing */
    SD_ERROR           /* anything else (timeout in the middle of a listing ...) */
} sd_status_t;

typedef struct { bool mounted; uint32_t total_mb; uint32_t used_mb; } sd_info_t;

sd_status_t SDBridge_Info(sd_info_t *info);
/* Up to `max` entries of `dir`, starting at entry number `first`. `total` = entries in the folder. */
sd_status_t SDBridge_List(const char *dir, uint16_t first, sd_entry_t *out, uint8_t max, uint8_t *got, uint16_t *total);
sd_status_t SDBridge_Play(const char *file);     /* ESP32-CAM decodes the MP3 and feeds the ADAU1401 */
sd_status_t SDBridge_Stop(void);
const char *SDBridge_StatusText(sd_status_t s);  /* <= 20 characters */

#endif
