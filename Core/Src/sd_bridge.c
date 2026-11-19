/**
  * @file    sd_bridge.c
  * @brief   File names on the ESP32-CAM's SD card, fetched over the shared UART2 (see sd_proto.h).
  */
#include "sd_bridge.h"
#include "wifi_manager.h"
#include "wifi_net.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/* Send one command line and hand every reply line to `cb` until OK / ERROR / timeout.
 * Returns 1 = OK seen, 0 = ERROR seen, -1 = silence (timeout), -2 = firmware said "busy" three times. */
typedef void (*line_cb_t)(const char *line, void *ctx);

static int run(const char *cmd, uint32_t timeout_ms, line_cb_t cb, void *ctx)
{
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        USART2_ClearRX();
        USART2_SendString(cmd);
        USART2_SendString("\r\n");
        char line[128];
        uint32_t start = HAL_GetTick();
        bool busy = false;
        while ((HAL_GetTick() - start) < timeout_ms) {
            if (USART2_ReceiveLine(line, sizeof line, 50) != HAL_OK) continue;
            if (strstr(line, "busy") != NULL) { busy = true; break; }
            if (cb) cb(line, ctx);
            if (strcmp(line, "OK") == 0) return 1;
            if (strstr(line, "ERROR") != NULL) return 0;
        }
        if (!busy) return -1;
        HAL_Delay(300);
    }
    return -2;
}

static bool link_ready(void)
{
    return ESP01S_EnsureReady();
}

typedef struct { sd_info_t *info; bool got; } info_ctx_t;
static void info_cb(const char *l, void *c)
{
    info_ctx_t *x = (info_ctx_t *)c;
    if (sdp_parse_info(l, &x->info->mounted, &x->info->total_mb, &x->info->used_mb)) x->got = true;
}

sd_status_t SDBridge_Info(sd_info_t *info)
{
    memset(info, 0, sizeof *info);
    if (!link_ready()) return SD_NO_LINK;
    info_ctx_t c = { info, false };
    int r = run("AT+SDINFO", 3000, info_cb, &c);
    if (r == -1 || r == -2) return SD_NO_LINK;
    if (r == 0 || !c.got) return SD_NO_BRIDGE;
    return info->mounted ? SD_OK : SD_NO_CARD;
}

typedef struct { sd_entry_t *out; uint8_t max, got; uint16_t total; bool end_seen; bool err; } ls_ctx_t;
static void ls_cb(const char *l, void *c)
{
    ls_ctx_t *x = (ls_ctx_t *)c;
    sd_entry_t e;
    if (sdp_parse_entry(l, &e)) { if (x->got < x->max) x->out[x->got++] = e; }
    else if (sdp_parse_end(l, &x->total)) x->end_seen = true;
    else if (strstr(l, "+SDERR") != NULL) x->err = true;
}

sd_status_t SDBridge_List(const char *dir, uint16_t first, sd_entry_t *out, uint8_t max, uint8_t *got, uint16_t *total)
{
    *got = 0; *total = 0;
    if (!link_ready()) return SD_NO_LINK;
    char esc[2 * 64 + 4];
    wnet_escape_at(dir, esc, sizeof esc);
    char cmd[2 * 64 + 40];
    snprintf(cmd, sizeof cmd, "AT+SDLS=\"%s\",%u,%u", esc, (unsigned)first, (unsigned)max);
    ls_ctx_t c = { out, max, 0, 0, false, false };
    int r = run(cmd, 4000, ls_cb, &c);
    *got = c.got; *total = c.total;
    if (r == -1 || r == -2) return c.got ? SD_ERROR : SD_NO_LINK;
    if (r == 0 || c.err) return SD_BAD_PATH;
    return c.end_seen ? SD_OK : SD_ERROR;
}

sd_status_t SDBridge_Play(const char *file)
{
    if (!link_ready()) return SD_NO_LINK;
    char esc[2 * 64 + 4];
    wnet_escape_at(file, esc, sizeof esc);
    char cmd[2 * 64 + 24];
    snprintf(cmd, sizeof cmd, "AT+SDPLAY=\"%s\"", esc);
    int r = run(cmd, 3000, NULL, NULL);
    return r == 1 ? SD_OK : (r == 0 ? SD_BAD_PATH : SD_NO_LINK);
}

sd_status_t SDBridge_Stop(void)
{
    if (!link_ready()) return SD_NO_LINK;
    return run("AT+SDSTOP", 2000, NULL, NULL) == 1 ? SD_OK : SD_ERROR;
}

const char *SDBridge_StatusText(sd_status_t s)
{
    switch (s) {
    case SD_OK:        return "OK";
    case SD_NO_LINK:   return "NO ESP32-CAM LINK";
    case SD_NO_BRIDGE: return "CAM: NO SD FIRMWARE";
    case SD_NO_CARD:   return "NO SD CARD IN CAM";
    case SD_BAD_PATH:  return "FOLDER NOT FOUND";
    default:           return "SD READ ERROR";
    }
}
