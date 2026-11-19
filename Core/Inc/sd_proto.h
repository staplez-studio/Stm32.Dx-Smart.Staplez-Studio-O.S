#ifndef SD_PROTO_H
#define SD_PROTO_H
/* Text protocol spoken with the ESP32-CAM about ITS micro SD card (same UART as the Wi-Fi commands, AT style):
 *
 *   AT+SDINFO                      ->  +SDINFO:<mounted 0|1>,<total MB>,<used MB>      OK
 *   AT+SDLS="<dir>",<first>,<n>    ->  +SDLS:<d|f>,<size in bytes>,"<name>"  (n lines at most)
 *                                      +SDLS:END,<entries in the folder>               OK
 *   AT+SDPLAY="<file>"             ->  OK   (the ESP32-CAM decodes the MP3 and sends it to the ADAU1401 over I2S)
 *   AT+SDSTOP                      ->  OK
 *
 * Only the parsing lives here (no hardware), so it can be unit-tested on a PC. */
#include <stdint.h>
#include <stdbool.h>

#define SD_NAME_MAX 24

typedef struct {
    char     name[SD_NAME_MAX];   /* printable ASCII, cut to fit */
    uint32_t size;
    bool     is_dir;
} sd_entry_t;

bool sdp_parse_info(const char *line, bool *mounted, uint32_t *total_mb, uint32_t *used_mb);
bool sdp_parse_entry(const char *line, sd_entry_t *e);
bool sdp_parse_end(const char *line, uint16_t *total);
bool sdp_is_mp3(const char *name);
void sdp_format_size(uint32_t bytes, char out[6]);        /* "512B" "12K" "1.2M" "3.4G" */
bool sdp_path_join(char *path, uint16_t cap, const char *name);   /* "/MUSIC" + "a" -> "/MUSIC/a" */
void sdp_path_parent(char *path);                          /* "/MUSIC/a" -> "/MUSIC", "/MUSIC" -> "/" */

#endif
