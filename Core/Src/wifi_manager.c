/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    wifi_manager.c
  * @brief   Wi-Fi driver and screens for an ESP8266 / ESP32 running Espressif AT firmware.
  *
  *          Features: module start-up and sync, real network scan, join with a usable password
  *          editor, saved network (EEPROM) with auto-join, network time -> DS3231, plain HTTP GET
  *          (used by the weather screen).
  ******************************************************************************
  */
/* USER CODE END Header */
#include "wifi_manager.h"
#include "wifi_config.h"
#include "wifi_net.h"
#include "clock_zone.h"
#include "staplez_studio.h"
#include "sh1106.h"
#include "usart.h"
#include "i2c.h"
#include "ui_draw.h"
#include "weather_icons.h"
#include "alarm_manager.h"
#include "weather_app.h"
#include "wifi_store.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------------------------ */
/*  State                                                                                      */
/* ------------------------------------------------------------------------------------------ */
bool wifi_connected = false;
ESP01S_State_t esp01s_state = ESP01S_STATE_IDLE;
char esp01s_ip_address[ESP01S_IP_STR_LEN] = "0.0.0.0";
char esp01s_active_ssid[ESP01S_MAX_SSID_LEN] = "";
char esp01s_status_msg[48] = "NOT STARTED";

/* Starts EMPTY: only networks the module really found are ever listed. */
uint8_t esp01s_scanned_count = 0;
WiFi_AP_Entry_t esp01s_scanned_aps[ESP01S_MAX_SCAN_APS];

#define PWD_MAX 63U

static uint8_t wifi_scroll_offset = 0;
static uint8_t wifi_selected_index = 0;
/* 0 = AP list, 1 = SCAN, 2 = SYNC time, 3 = DISC, 4 = BACK */
static uint8_t wifi_footer_focus = 0;

static char    target_ssid[ESP01S_MAX_SSID_LEN + 1];
static char    pwd_buffer[PWD_MAX + 1] = "";
static uint8_t pwd_len = 0;
static uint8_t pwd_cursor = 0;     /* 0..pwd_len (pwd_len = the empty slot after the last char) */
static uint8_t pwd_view = 0;       /* first character shown in the 16 slot window */
static uint8_t pwd_focus = 0;      /* 0 = characters, 1 = CONNECT (NEXT), 2 = BACK */
static uint8_t edit_mode = 0;      /* 0 = password, 1 = network name (add a hidden network) */
static bool    edit_hidden = false;/* the network being typed was added by name */
static uint8_t edit_max(void);

static char saved_ssid[ESP01S_MAX_SSID_LEN + 1] = "";
static char saved_pwd[PWD_MAX + 1] = "";
static bool saved_valid = false;

static uint32_t wifi_timer = 0;
static uint8_t  boot_stage = 0;    /* 0 = waiting for the module to start, 1 = start-up done */
static uint32_t time_sync_next = 0;   /* tick at which the next automatic time sync is due */
static bool     online_armed = false; /* true while connected: the online jobs have been scheduled */

typedef enum { V_LIST = 0, V_SAVED, V_ACTION } wview_t;
static wview_t view = V_LIST;

static bool     module_ok = false;        /* the module answered at start-up */
static bool     user_disconnected = false;/* DISCONNECT pressed: no automatic re-join until a network is chosen again */
static uint32_t rejoin_next = 0;          /* tick of the next automatic re-join attempt while offline */
static uint8_t  rejoin_fails = 0;
static uint32_t link_check_next = 0;      /* tick of the next "is the link still up?" check while online */
static char     skip_ssid[ESP01S_MAX_SSID_LEN + 1] = "";   /* saved network whose password was refused: not retried automatically */
static uint32_t last_scan_tick = 0;
static bool     scanned_once = false;

static void WiFi_Manager_Render(void);
static bool auto_join(void);
static bool link_alive(void);
static uint32_t rejoin_delay_ms(void);

/* ------------------------------------------------------------------------------------------ */
/*  Low level AT helpers                                                                       */
/* ------------------------------------------------------------------------------------------ */
static bool at_garbage_seen = false;   /* last command produced a line that is not valid AT text */

static bool line_is_garbage(const char *l)
{
    for (; *l; l++) {
        unsigned char c = (unsigned char)*l;
        if (c < 32 || c > 126) return true;
    }
    return false;
}

static bool at_once(const char *cmd, const char *expected, uint32_t timeout_ms, bool *busy)
{
    USART2_ClearRX();
    USART2_SendString(cmd);
    USART2_SendString("\r\n");

    char rx_line[128];
    uint32_t start = HAL_GetTick();
    *busy = false;

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (USART2_ReceiveLine(rx_line, sizeof(rx_line), 25) == HAL_OK) {
            if (line_is_garbage(rx_line)) at_garbage_seen = true;
            if (strstr(rx_line, expected) != NULL) return true;
            if (strstr(rx_line, "busy") != NULL) { *busy = true; return false; }
            if (strstr(rx_line, "ERROR") != NULL || strstr(rx_line, "FAIL") != NULL) return false;
        }
    }
    return false;
}

/* Older firmware answers "busy p..." / "busy s..." while it is still working: wait and ask again. */
static bool ESP01S_SendATCommand(const char *cmd, const char *expected, uint32_t timeout_ms)
{
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        bool busy;
        if (at_once(cmd, expected, timeout_ms, &busy)) return true;
        if (!busy) return false;
        HAL_Delay(400);
    }
    return false;
}

static uint32_t status_tick = 0;   /* when the status text last changed */

static void set_status(const char *msg)
{
    snprintf(esp01s_status_msg, sizeof(esp01s_status_msg), "%s", msg);
    status_tick = HAL_GetTick();
}

static void set_statusf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(esp01s_status_msg, sizeof(esp01s_status_msg), fmt, ap);
    va_end(ap);
    status_tick = HAL_GetTick();
}

/* Draw the screen right now (used before a call that blocks for several seconds) */
static void show_busy(const char *msg)
{
    set_status(msg);
    WiFi_Manager_Render();
}

/* Is the module already joined to an access point (e.g. it kept the link)? */
static void detect_existing_link(void)
{
    if (!ESP01S_QueryIP()) return;
    if (strcmp(esp01s_ip_address, "0.0.0.0") == 0 || esp01s_ip_address[0] == '\0') return;

    USART2_ClearRX();
    USART2_SendString("AT+CWJAP?\r\n");
    char line[128];
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 600) {
        if (USART2_ReceiveLine(line, sizeof(line), 100) == HAL_OK) {
            char *q = strstr(line, "+CWJAP:\"");
            if (q) {
                q += 8;
                char *e = strchr(q, '"');
                if (e) {
                    size_t n = (size_t)(e - q);
                    if (n >= sizeof(esp01s_active_ssid)) n = sizeof(esp01s_active_ssid) - 1;
                    memcpy(esp01s_active_ssid, q, n);
                    esp01s_active_ssid[n] = '\0';
                }
            }
            if (strstr(line, "OK") != NULL) break;
        }
    }
    wifi_connected = true;
    esp01s_state = ESP01S_STATE_GOT_IP;
    set_status("ONLINE");
}

/* Speeds an ESP module may be set to. The first two are tried at every start; the full list only when the
 * user presses SCAN / SYNC or JOIN, so a missing module costs about half a second at power-up. */
static const uint32_t ESP_BAUDS[] = { 115200, 9600, 57600, 38400, 230400 };
#define ESP_BAUD_COUNT        ((uint8_t)(sizeof(ESP_BAUDS) / sizeof(ESP_BAUDS[0])))
#define ESP_BAUD_COUNT_QUICK  2U

/* Does anything answer AT at the current speed? */
static bool esp_probe_at(uint8_t tries)
{
    for (uint8_t i = 0; i < tries; i++) {
        if (ESP01S_SendATCommand("AT", "OK", 150)) return true;
        HAL_Delay(30);
    }
    return false;
}

static bool esp_sync_ex(uint8_t tries, bool all_bauds)
{
    esp01s_state = ESP01S_STATE_SYNCING;
    set_status("STARTING MODULE...");

    uint32_t rx_before = usart2_rx_total;
    uint32_t junk_before = usart2_rx_junk;
    bool at_ok = false;
    uint8_t nb = all_bauds ? ESP_BAUD_COUNT : ESP_BAUD_COUNT_QUICK;
    uint32_t first = USART2_GetBaud();

    at_garbage_seen = false;
    for (uint8_t b = 0; b < nb && !at_ok; b++) {
        if (USART2_GetBaud() != ESP_BAUDS[b]) USART2_SetBaud(ESP_BAUDS[b]);
        if (esp_probe_at(b == 0 ? tries : 2)) { at_ok = true; break; }
    }
    if (!at_ok && USART2_GetBaud() != first) USART2_SetBaud(ESP_BAUDS[0]);

    if (!at_ok) {
        esp01s_state = ESP01S_STATE_ERR_NO_MODULE;
        wifi_connected = false;
        if (usart2_rx_total == rx_before)  set_status("NO DATA: CHECK WIRES");   /* silence: TX/RX/power/EN */
        else if (at_garbage_seen || usart2_rx_junk != junk_before) set_status("GARBLED: WRONG BAUD");
        else                               set_status("NO AT: BAD FIRMWARE");  /* readable text, but not AT */
        return false;
    }

    ESP01S_SendATCommand("ATE0", "OK", 150);              /* no command echo */
    ESP01S_SendATCommand("AT+CWMODE=1", "OK", 250);       /* station mode (very old firmware: AT+CWMODE_CUR) */
    ESP01S_SendATCommand("AT+CWAUTOCONN=0", "OK", 150);   /* this firmware decides when to join (ignored if unsupported) */
    ESP01S_SendATCommand("AT+CWDHCP=1,1", "OK", 250);     /* DHCP client */
    ESP01S_SendATCommand("AT+CIPMUX=0", "OK", 250);       /* single connection (needed by HTTP) */

    esp01s_state = ESP01S_STATE_READY;
    set_status("MODULE READY");
    detect_existing_link();
    return true;
}

static bool esp_sync(uint8_t tries) { return esp_sync_ex(tries, false); }

bool ESP01S_SyncAndInit(void)
{
    return esp_sync_ex(6, true);     /* user-triggered: search every speed */
}

/* Make sure the module is started; cheap when it already is. */
bool ESP01S_EnsureReady(void);
static bool esp_ensure_ready(void)
{
    if (esp01s_state == ESP01S_STATE_READY || esp01s_state == ESP01S_STATE_CONNECTED ||
        esp01s_state == ESP01S_STATE_GOT_IP) {
        return true;
    }
    return esp_sync_ex(6, true);
}

bool ESP01S_EnsureReady(void) { return esp_ensure_ready(); }

/* ------------------------------------------------------------------------------------------ */
/*  Scan                                                                                       */
/* ------------------------------------------------------------------------------------------ */
bool ESP01S_ScanNetworks(void)
{
    if (!esp_ensure_ready()) return false;

    esp01s_state = ESP01S_STATE_SCANNING;
    esp01s_scanned_count = 0;
    wifi_selected_index = 0;
    wifi_scroll_offset = 0;

    USART2_ClearRX();
    USART2_SendString("AT+CWLAP\r\n");

    char line[160];
    uint32_t start = HAL_GetTick();
    uint8_t count = 0;

    while ((HAL_GetTick() - start) < 9000) {
        if (USART2_ReceiveLine(line, sizeof(line), 500) != HAL_OK) continue;

        char *tag = strstr(line, "+CWLAP:(");
        if (tag) {
            /* +CWLAP:(<ecn>,"<ssid>",<rssi>,"<mac>",<channel>,...) */
            int ecn = atoi(tag + 8);
            char *first_quote = strchr(tag, '"');
            char *end_quote = first_quote ? strstr(first_quote + 1, "\",") : NULL;
            if (first_quote && end_quote && end_quote > first_quote + 1) {
                size_t len = (size_t)(end_quote - (first_quote + 1));
                if (len >= ESP01S_MAX_SSID_LEN) len = ESP01S_MAX_SSID_LEN - 1;
                char ssid[ESP01S_MAX_SSID_LEN];
                memcpy(ssid, first_quote + 1, len);
                ssid[len] = '\0';
                int rssi = atoi(end_quote + 2);

                /* keep only the strongest entry for each name */
                int dup = -1;
                for (uint8_t i = 0; i < count; i++) {
                    if (strcmp(esp01s_scanned_aps[i].ssid, ssid) == 0) { dup = i; break; }
                }
                if (dup >= 0) {
                    if (rssi > esp01s_scanned_aps[dup].rssi) {
                        esp01s_scanned_aps[dup].rssi = (int8_t)rssi;
                        esp01s_scanned_aps[dup].encryption = (uint8_t)ecn;
                    }
                } else {
                    int slot = -1;
                    if (count < ESP01S_MAX_SCAN_APS) {
                        slot = count++;
                    } else {
                        /* list full (crowded area): replace the weakest entry, but never push out a saved network */
                        int weakest = -1, wscore = 1000;
                        for (uint8_t i = 0; i < count; i++) {
                            int sc = esp01s_scanned_aps[i].rssi + (WStore_Find(esp01s_scanned_aps[i].ssid) >= 0 ? 200 : 0);
                            if (sc < wscore) { wscore = sc; weakest = i; }
                        }
                        int mine = rssi + (WStore_Find(ssid) >= 0 ? 200 : 0);
                        if (weakest >= 0 && mine > wscore) slot = weakest;
                    }
                    if (slot >= 0) {
                        memcpy(esp01s_scanned_aps[slot].ssid, ssid, len + 1);
                        esp01s_scanned_aps[slot].rssi = (int8_t)rssi;
                        esp01s_scanned_aps[slot].encryption = (uint8_t)ecn;
                    }
                }
            }
        }
        if (strstr(line, "OK") != NULL || strstr(line, "ERROR") != NULL) {
            break;
        }
    }

    /* strongest first */
    for (uint8_t i = 1; i < count; i++) {
        WiFi_AP_Entry_t key = esp01s_scanned_aps[i];
        int j = (int)i - 1;
        while (j >= 0 && esp01s_scanned_aps[j].rssi < key.rssi) {
            esp01s_scanned_aps[j + 1] = esp01s_scanned_aps[j];
            j--;
        }
        esp01s_scanned_aps[j + 1] = key;
    }

    esp01s_scanned_count = count;
    esp01s_state = wifi_connected ? ESP01S_STATE_GOT_IP : ESP01S_STATE_READY;
    if (count == 0) {
        set_status("NO NETWORKS FOUND");
    } else {
        set_statusf("FOUND %u NETWORKS", (unsigned)count);
    }
    return true;
}

/* ------------------------------------------------------------------------------------------ */
/*  IP / connect / disconnect                                                                  */
/* ------------------------------------------------------------------------------------------ */
bool ESP01S_QueryIP(void)
{
    char line[128];
    bool found = false;

    for (uint8_t attempt = 0; attempt < 3 && !found; attempt++) {
    USART2_ClearRX();
    USART2_SendString("AT+CIFSR\r\n");
    uint32_t start = HAL_GetTick();
    bool busy = false;

    while ((HAL_GetTick() - start) < 1500) {
        if (USART2_ReceiveLine(line, sizeof(line), 250) == HAL_OK) {
            if (strstr(line, "busy") != NULL) { busy = true; break; }
            char *ip_tag = strstr(line, "+CIFSR:STAIP,\"");
            if (ip_tag) {
                char *start_ip = ip_tag + 14;
                char *end_ip = strchr(start_ip, '"');
                if (end_ip) {
                    size_t len = (size_t)(end_ip - start_ip);
                    if (len >= ESP01S_IP_STR_LEN) len = ESP01S_IP_STR_LEN - 1;
                    memcpy(esp01s_ip_address, start_ip, len);
                    esp01s_ip_address[len] = '\0';
                    found = true;
                }
            }
            if (strstr(line, "OK") != NULL) break;
        }
    }
    if (!busy) break;
    HAL_Delay(400);
    }
    return found;
}

bool ESP01S_ConnectAP(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0') return false;
    if (!esp_ensure_ready()) return false;

    esp01s_state = ESP01S_STATE_CONNECTING;
    wifi_connected = false;
    set_status("CONNECTING...");

    char esc_ssid[2 * ESP01S_MAX_SSID_LEN + 4];
    char esc_pwd[2 * PWD_MAX + 4];
    wnet_escape_at(ssid, esc_ssid, sizeof(esc_ssid));
    wnet_escape_at(password ? password : "", esc_pwd, sizeof(esc_pwd));

    char cmd[2 * ESP01S_MAX_SSID_LEN + 2 * PWD_MAX + 24];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", esc_ssid, esc_pwd);

    USART2_ClearRX();
    USART2_SendString(cmd);
    USART2_SendString("\r\n");

    char line[128];
    uint32_t start = HAL_GetTick();
    bool got_link = false;
    bool got_dhcp = false;

    while ((HAL_GetTick() - start) < 20000) {
        if (USART2_ReceiveLine(line, sizeof(line), 300) != HAL_OK) continue;

        if (strstr(line, "WIFI CONNECTED") != NULL) {
            got_link = true;
            set_status("LINK UP, GETTING IP");
        }
        if (strstr(line, "WIFI GOT IP") != NULL) {
            got_dhcp = true;
            set_status("GOT IP ADDRESS");
        }
        if (strstr(line, "+CWJAP:1") != NULL) {
            esp01s_state = ESP01S_STATE_ERR_TIMEOUT;
            set_status("ERR: NO RESPONSE");
            return false;
        }
        if (strstr(line, "+CWJAP:2") != NULL) {
            esp01s_state = ESP01S_STATE_ERR_WRONG_PWD;
            set_status("ERR: WRONG PASSWORD");
            return false;
        }
        if (strstr(line, "+CWJAP:3") != NULL) {
            esp01s_state = ESP01S_STATE_ERR_NOT_FOUND;
            set_status("ERR: NETWORK NOT FOUND");
            return false;
        }
        if (strstr(line, "+CWJAP:4") != NULL) {
            esp01s_state = ESP01S_STATE_ERR_TIMEOUT;
            set_status("ERR: CONNECT FAILED");
            return false;
        }
        if (strstr(line, "FAIL") != NULL) {
            esp01s_state = ESP01S_STATE_ERR_TIMEOUT;
            if (strcmp(esp01s_status_msg, "CONNECTING...") == 0) set_status("ERR: CONNECT FAILED");
            return false;
        }
        if (strstr(line, "OK") != NULL) {
            /* Old firmware prints only "OK" (no WIFI CONNECTED / GOT IP lines): confirm with the IP address. */
            if (!got_link && !got_dhcp) {
                if (!ESP01S_QueryIP() || strcmp(esp01s_ip_address, "0.0.0.0") == 0) continue;
            }
            snprintf(esp01s_active_ssid, sizeof(esp01s_active_ssid), "%s", ssid);
            wifi_connected = true;
            esp01s_state = ESP01S_STATE_GOT_IP;
            if (!ESP01S_QueryIP()) snprintf(esp01s_ip_address, sizeof(esp01s_ip_address), "0.0.0.0");
            set_status("ONLINE");
            return true;
        }
    }

    esp01s_state = ESP01S_STATE_ERR_TIMEOUT;
    set_status("ERR: TIMEOUT");
    return false;
}

bool ESP01S_Disconnect(void)
{
    ESP01S_SendATCommand("AT+CWQAP", "OK", 500);
    wifi_connected = false;
    esp01s_state = ESP01S_STATE_READY;
    esp01s_active_ssid[0] = '\0';
    snprintf(esp01s_ip_address, sizeof(esp01s_ip_address), "0.0.0.0");
    set_status("DISCONNECTED");
    return true;
}

/* ------------------------------------------------------------------------------------------ */
/*  Saved networks (EEPROM, see wifi_store.c). saved_ssid/saved_pwd mirror the most recent one.  */
/* ------------------------------------------------------------------------------------------ */
static void sync_saved_compat(void)
{
    const wstore_net_t *n = WStore_Get(0);
    memset(saved_ssid, 0, sizeof(saved_ssid));
    memset(saved_pwd, 0, sizeof(saved_pwd));
    saved_valid = (n != NULL);
    if (n) { snprintf(saved_ssid, sizeof(saved_ssid), "%s", n->ssid); snprintf(saved_pwd, sizeof(saved_pwd), "%s", n->pwd); }
}

bool WiFi_Creds_Load(void)
{
    WStore_Load();
    sync_saved_compat();
    return saved_valid;
}

bool WiFi_Creds_Save(const char *ssid, const char *password)
{
    bool r = WStore_Save(ssid, password);
    sync_saved_compat();
    return r;
}

void WiFi_Creds_Clear(void)                       /* forget every saved network */
{
    while (WStore_Count()) WStore_Forget(WStore_Get(0)->ssid);
    sync_saved_compat();
}

/* ------------------------------------------------------------------------------------------ */
/*  Network time                                                                               */
/* ------------------------------------------------------------------------------------------ */
/* Time source 1: the module's own SNTP client (AT+CIPSNTPCFG). */
static bool get_utc_sntp(wnet_time_t *utc)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CIPSNTPCFG=1,0,\"%s\"", WIFI_NTP_SERVER);
    if (!ESP01S_SendATCommand(cmd, "OK", 1500)) return false;     /* firmware without SNTP */

    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 12000) {
        USART2_ClearRX();
        USART2_SendString("AT+CIPSNTPTIME?\r\n");
        char line[96];
        uint32_t t0 = HAL_GetTick();
        while ((HAL_GetTick() - t0) < 800) {
            if (USART2_ReceiveLine(line, sizeof(line), 200) == HAL_OK) {
                if (wnet_parse_sntp(line, utc)) return true;
                if (strstr(line, "ERROR") != NULL) break;
            }
        }
        HAL_Delay(500);
    }
    return false;
}

/* Time source 2 (firmware without SNTP, or SNTP blocked): the "Date:" header of any web server. It is UTC. */
static bool get_utc_http(wnet_time_t *utc)
{
    static char resp[320];
    char req[96];
    snprintf(req, sizeof(req), "HEAD / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", WEATHER_HOST);
    if (ESP01S_HttpGet(WEATHER_HOST, 80, req, resp, sizeof(resp), 8000) == 0) return false;
    return wnet_parse_http_date(resp, utc);
}

bool ESP01S_SyncTime(void)
{
    if (!wifi_connected) {
        set_status("NEED WIFI FIRST");
        return false;
    }

    wnet_time_t utc;
    if (!get_utc_sntp(&utc) && !get_utc_http(&utc)) {
        set_status("NO NETWORK TIME");
        return false;
    }

    /* Network time is UTC. The DS3231 holds standard time, so apply only the zone's standard offset;
     * daylight saving is added when the time is read back. */
    wnet_time_t std;
    tz_utc_to_std(&tz_config, &utc, &std);

    if (ds3231_present) {
        DS3231_SetStdTime(&std);
        DS3231_GetTime();                       /* refresh current_* (local time, DST applied) */
    } else {
        wnet_time_t l;
        bool dst;
        tz_std_to_local(&tz_config, &std, &l, &dst);
        current_year = l.year; current_month = l.month; current_day_num = l.day; current_wday = l.wday;
        current_hour = (uint8_t)((l.hour % 12U) ? (l.hour % 12U) : 12U);
        current_minute = l.minute; current_second = l.second;
        is_pm = (l.hour >= 12) ? 1 : 0;
        tz_dst_now = dst; tz_std_now = std;
    }
    set_statusf("TIME SET %02u:%02u%c", (unsigned)current_hour, (unsigned)current_minute, is_pm ? 'P' : 'A');
    time_sync_next = HAL_GetTick() + (uint32_t)WIFI_TIME_SYNC_HOURS * 3600000UL;   /* the manual sync counts too */
    return true;
}

/* ------------------------------------------------------------------------------------------ */
/*  HTTP GET over the module's TCP connection                                                  */
/* ------------------------------------------------------------------------------------------ */
static bool wait_for_prompt(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t b;
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (USART2_ReadByte(&b) && b == '>') return true;
    }
    return false;
}

uint16_t ESP01S_HttpGet(const char *host, uint16_t port, const char *request,
                        char *out, uint16_t cap, uint32_t timeout_ms)
{
    if (!wifi_connected || cap < 16) return 0;
    out[0] = '\0';

    char cmd[96];
    ESP01S_SendATCommand("AT+CIPCLOSE", "OK", 200);          /* drop a connection left open by an earlier failure (ERROR is fine) */
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, (unsigned)port);
    if (!ESP01S_SendATCommand(cmd, "OK", 9000)) {
        ESP01S_SendATCommand("AT+CIPCLOSE", "OK", 300);
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)strlen(request));
    USART2_ClearRX();
    USART2_SendString(cmd);
    USART2_SendString("\r\n");
    if (!wait_for_prompt(3000)) {
        ESP01S_SendATCommand("AT+CIPCLOSE", "OK", 300);
        return 0;
    }
    USART2_SendString(request);

    /* Read "+IPD,<n>:<n bytes>" blocks until the server closes the connection. Byte-wise because the
     * payload itself contains line breaks. */
    enum { S_SEARCH, S_LEN, S_SKIP, S_DATA } st = S_SEARCH;
    uint8_t win[6] = {0};
    uint16_t len_acc = 0, remaining = 0, got = 0;
    bool closed = false;
    uint32_t start = HAL_GetTick();

    while (!closed && (HAL_GetTick() - start) < timeout_ms) {
        uint8_t b;
        if (!USART2_ReadByte(&b)) continue;

        switch (st) {
        case S_SEARCH:
            for (uint8_t i = 0; i < 5; i++) win[i] = win[i + 1];
            win[5] = b;
            if (win[1] == '+' && win[2] == 'I' && win[3] == 'P' && win[4] == 'D' && win[5] == ',') {
                st = S_LEN;
                len_acc = 0;
            } else if (memcmp(win, "CLOSED", 6) == 0) {
                closed = true;
            }
            break;
        case S_LEN:
            if (b >= '0' && b <= '9') {
                len_acc = (uint16_t)(len_acc * 10U + (b - '0'));
            } else if (b == ':') {
                remaining = len_acc;
                st = remaining ? S_DATA : S_SEARCH;
            } else if (b == ',') {
                st = S_SKIP;                  /* "+IPD,<len>,"<ip>",<port>:" when CIPDINFO is on */
            } else {
                st = S_SEARCH;
            }
            break;
        case S_SKIP:
            if (b == ':') { remaining = len_acc; st = remaining ? S_DATA : S_SEARCH; }
            break;
        case S_DATA:
            if (got + 1U < cap) out[got++] = (char)b;
            if (--remaining == 0) { st = S_SEARCH; memset(win, 0, sizeof(win)); }
            break;
        }
    }
    out[got] = '\0';

    if (!closed) ESP01S_SendATCommand("AT+CIPCLOSE", "OK", 300);
    return got;
}

/* ------------------------------------------------------------------------------------------ */
/*  Start-up                                                                                   */
/* ------------------------------------------------------------------------------------------ */
void WiFi_Manager_Init(void)
{
    MX_USART2_Init();
    wifi_scroll_offset = 0;
    wifi_selected_index = 0;
    wifi_footer_focus = 0;
    pwd_cursor = 0;
    pwd_focus = 0;
    wifi_connected = false;
    esp01s_state = ESP01S_STATE_IDLE;
    esp01s_active_ssid[0] = '\0';
    snprintf(esp01s_ip_address, sizeof(esp01s_ip_address), "0.0.0.0");
    set_status("NOT STARTED");
    boot_stage = 0;
    view = V_LIST; module_ok = false; user_disconnected = false; scanned_once = false; last_scan_tick = 0;
    skip_ssid[0] = '\0'; rejoin_fails = 0; rejoin_next = 0; link_check_next = 0;
    /* The module is probed later from WiFi_Manager_Background(): at power-up an ESP needs 1-3 s
     * before it answers, so a probe right here always failed. */
}

/* Short full-screen note so a pause of a few seconds is explained */
static void overlay(const char *line2)
{
    sh1106_clear();
    sh1106_drawString(4, 20, "WIFI", 1, SH1106_WHITE);
    sh1106_drawString(4, 32, line2, 1, SH1106_WHITE);
    sh1106_update();
}

/* Online jobs only run where a pause of a few seconds is harmless */
static bool online_job_allowed(void)
{
    if (is_alarm_ringing) return false;
    return current_state == STATE_MAIN_SCREEN || current_state == STATE_WIFI_APP;
}

void WiFi_Manager_Background(uint32_t current_tick)
{
    /* ---- once, a few seconds after power-up: start the module and join the saved network ---- */
    if (boot_stage == 0) {
        if (current_tick < WIFI_BOOT_PROBE_DELAY_MS) return;
        boot_stage = 1;

        WiFi_Creds_Load();
        if (!esp_sync(3)) return;           /* no module attached: costs about half a second once */
        module_ok = true;

#if WIFI_AUTOCONNECT
        if (WStore_Count() > 0 && !wifi_connected) {
            if (!auto_join()) { rejoin_fails = 1; rejoin_next = HAL_GetTick() + rejoin_delay_ms(); }
        }
#endif
        return;
    }

    /* ---- keep the link: notice a dropped connection, and look for a known network when offline ---- */
    if (module_ok && wifi_connected && online_job_allowed() && (int32_t)(current_tick - link_check_next) >= 0) {
        link_check_next = current_tick + 45000UL;
        if (!link_alive()) {
            wifi_connected = false;
            esp01s_active_ssid[0] = '\0';
            set_status("WIFI LOST");
            rejoin_fails = 0;
            rejoin_next = current_tick + rejoin_delay_ms();
        }
    }
#if WIFI_AUTOCONNECT
    if (module_ok && !wifi_connected && !user_disconnected && WStore_Count() > 0 && online_job_allowed() &&
        (int32_t)(current_tick - rejoin_next) >= 0 && current_state == STATE_MAIN_SCREEN) {
        if (auto_join()) {
            link_check_next = HAL_GetTick() + 45000UL;
        } else {
            if (rejoin_fails < 250) rejoin_fails++;
            rejoin_next = HAL_GetTick() + rejoin_delay_ms();
        }
        return;
    }
#endif

    /* ---- whenever online: set the clock, then fetch the weather ---- */
    if (!wifi_connected) {
        online_armed = false;
        return;
    }
    if (!online_job_allowed()) return;

    if (!online_armed) {                    /* just came online: run the jobs now */
        online_armed = true;
        time_sync_next = current_tick;
    }

#if WIFI_AUTO_TIME_SYNC
    if ((int32_t)(current_tick - time_sync_next) >= 0) {
#if WIFI_AUTOSYNC_REQUIRES_ZONE
        if (!tz_saved) {
            set_status("SET TIME ZONE FIRST");     /* Set Time > zone > SAVE */
            time_sync_next = current_tick + 60000UL;
            return;
        }
#endif
        overlay("SYNCING TIME...");
        if (!ESP01S_SyncTime()) {
            time_sync_next = current_tick + 600000UL;   /* try again in 10 minutes */
        }                                               /* on success ESP01S_SyncTime() schedules the next one */
        return;                                         /* one job per pass */
    }
#endif

#if WEATHER_AUTO_REFRESH
    Weather_Background(current_tick);
#endif
}

/* ------------------------------------------------------------------------------------------ */
/*  Joining saved networks                                                                     */
/* ------------------------------------------------------------------------------------------ */
static void note_join_result(const char *ssid, bool ok)
{
    if (ok) {
        if (ssid && strcmp(skip_ssid, ssid) == 0) skip_ssid[0] = '\0';   /* it works now: stop avoiding it */
        user_disconnected = false;
        rejoin_fails = 0;
    } else if (esp01s_state == ESP01S_STATE_ERR_WRONG_PWD && ssid) {
        snprintf(skip_ssid, sizeof(skip_ssid), "%s", ssid);
    }
}

/* Join a network that is in the saved list (index into the list). */
static bool join_saved(const char *ssid)
{
    int k = WStore_Find(ssid);
    if (k < 0) return false;
    wstore_net_t n = *WStore_Get((uint8_t)k);
    char msg[24];
    snprintf(msg, sizeof(msg), "JOINING %.12s", n.ssid);
    overlay(msg);
    bool ok = ESP01S_ConnectAP(n.ssid, n.pwd);
    note_join_result(n.ssid, ok);
    if (ok) { WStore_Touch(n.ssid); sync_saved_compat(); }
    return ok;
}

/* Look around, then join the best saved network that is in range (strongest signal first, up to 3 tries). */
static bool auto_join(void)
{
    if (WStore_Count() == 0) return false;
    overlay("LOOKING FOR WIFI");
    bool scanned = ESP01S_ScanNetworks();
    if (scanned) { last_scan_tick = HAL_GetTick(); scanned_once = true; }

    uint8_t cand[WSTORE_MAX];
    uint8_t n = 0;
    if (scanned && esp01s_scanned_count > 0) {
        wstore_seen_t seen[ESP01S_MAX_SCAN_APS];
        for (uint8_t i = 0; i < esp01s_scanned_count; i++) { seen[i].ssid = esp01s_scanned_aps[i].ssid; seen[i].rssi = esp01s_scanned_aps[i].rssi; }
        n = WStore_Candidates(seen, esp01s_scanned_count, cand, WSTORE_MAX);
    } else if (!scanned) {
        cand[0] = 0; n = 1;                                   /* scan failed: just try the most recent network */
    }
    bool any_hidden = false;
    for (uint8_t i = 0; i < WStore_Count(); i++) if (WStore_Get(i)->hidden) any_hidden = true;
    if (n == 0 && !any_hidden) { set_status("NO KNOWN WIFI NEARBY"); return false; }

    uint8_t tries = 0;
    for (uint8_t i = 0; i < n && tries < 3; i++) {
        const wstore_net_t *net = WStore_Get(cand[i]);
        if (!net || strcmp(net->ssid, skip_ssid) == 0) continue;
        tries++;
        char name[WSTORE_SSID_MAX + 1];
        snprintf(name, sizeof(name), "%s", net->ssid);
        if (join_saved(name)) return true;
    }
    /* Hidden networks never appear in a scan: try those by name once the visible ones are used up. */
    for (uint8_t i = 0; i < WStore_Count() && tries < 3; i++) {
        const wstore_net_t *net = WStore_Get(i);
        if (!net->hidden || strcmp(net->ssid, skip_ssid) == 0) continue;
        char name[WSTORE_SSID_MAX + 1];
        snprintf(name, sizeof(name), "%s", net->ssid);
        tries++;
        if (join_saved(name)) return true;
    }
    return false;
}

static bool link_alive(void)
{
    char before[ESP01S_IP_STR_LEN];
    snprintf(before, sizeof(before), "%s", esp01s_ip_address);
    if (!ESP01S_QueryIP()) { snprintf(esp01s_ip_address, sizeof(esp01s_ip_address), "%s", before); return true; }   /* no answer: do not guess */
    return strcmp(esp01s_ip_address, "0.0.0.0") != 0 && esp01s_ip_address[0] != '\0';
}

static uint32_t rejoin_delay_ms(void)
{
    static const uint32_t d[] = { 15000UL, 45000UL, 120000UL, 300000UL, 600000UL };
    return d[rejoin_fails < 5 ? rejoin_fails : 4];
}

static bool scan_rssi(const char *ssid, int8_t *rssi)
{
    for (uint8_t i = 0; i < esp01s_scanned_count; i++)
        if (strcmp(esp01s_scanned_aps[i].ssid, ssid) == 0) { *rssi = esp01s_scanned_aps[i].rssi; return true; }
    return false;
}

/* ------------------------------------------------------------------------------------------ */
/*  Password editor                                                                            */
/* ------------------------------------------------------------------------------------------ */
static const char PWD_CHARS[] =
    "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()-_=+[]{};:'\",.<>/?\\|`~ ";

static char next_char(char c, int dir)
{
    const char *p = (c != '\0') ? strchr(PWD_CHARS, c) : NULL;
    int n = (int)(sizeof(PWD_CHARS) - 1U);
    int idx = p ? (int)(p - PWD_CHARS) : (dir > 0 ? -1 : 0);
    idx = (idx + dir + n) % n;
    return PWD_CHARS[idx];
}

static void pwd_fix_view(void)
{
    if (pwd_cursor < pwd_view) pwd_view = pwd_cursor;
    if (pwd_cursor >= pwd_view + 16U) pwd_view = (uint8_t)(pwd_cursor - 15U);
}

static uint8_t edit_max(void) { return edit_mode ? (uint8_t)ESP01S_MAX_SSID_LEN : (uint8_t)PWD_MAX; }

static void pwd_open_h(const char *ssid, bool hidden)
{
    edit_mode = 0;
    edit_hidden = hidden;
    snprintf(target_ssid, sizeof(target_ssid), "%s", ssid);
    int k = WStore_Find(ssid);
    if (k >= 0) {
        snprintf(pwd_buffer, sizeof(pwd_buffer), "%s", WStore_Get((uint8_t)k)->pwd);   /* known network: pre-fill */
    } else {
        pwd_buffer[0] = '\0';
    }
    pwd_len = (uint8_t)strlen(pwd_buffer);
    pwd_cursor = pwd_len;
    pwd_view = 0;
    pwd_fix_view();
    pwd_focus = 0;
    set_status("");
    set_system_state(STATE_WIFI_PASSWORD);
}

static void pwd_open(const char *ssid) { pwd_open_h(ssid, false); }

static void name_open(void)                   /* type the name of a hidden / not listed network */
{
    edit_mode = 1;
    pwd_buffer[0] = '\0';
    target_ssid[0] = '\0';
    pwd_len = 0; pwd_cursor = 0; pwd_view = 0; pwd_focus = 0;
    set_status("");
    set_system_state(STATE_WIFI_PASSWORD);
}

static void pwd_do_connect(void)
{
    if (edit_mode) {                          /* name typed: now ask for its password */
        if (pwd_len == 0) { set_status("ENTER A NAME"); return; }
        char nm[ESP01S_MAX_SSID_LEN + 1];
        snprintf(nm, sizeof(nm), "%s", pwd_buffer);
        pwd_open_h(nm, true);
        return;
    }
    show_busy("CONNECTING...");
    bool ok = ESP01S_ConnectAP(target_ssid, pwd_buffer);
    note_join_result(target_ssid, ok);
    if (ok) {
        int8_t seen_r;
        WStore_SaveEx(target_ssid, pwd_buffer, edit_hidden && !scan_rssi(target_ssid, &seen_r));   /* remembered: it joins by itself next time */
        sync_saved_compat();
        view = V_LIST;
        set_system_state(STATE_WIFI_APP);
    }
    /* on failure stay here: the error is shown and the password can be corrected */
}

/* A number button types that digit at the cursor and moves on */
static void pwd_digit(char d)
{
    pwd_focus = 0;
    if (pwd_cursor >= pwd_len) {
        if (pwd_len >= edit_max()) return;
        pwd_buffer[pwd_len++] = d;
        pwd_buffer[pwd_len] = '\0';
        pwd_cursor = pwd_len;
    } else {
        pwd_buffer[pwd_cursor] = d;
        pwd_cursor++;
    }
    pwd_fix_view();
}

void WiFi_Manager_Digit(char digit)
{
    if (current_state != STATE_WIFI_PASSWORD) return;
    pwd_digit(digit);
    wifi_timer = HAL_GetTick();
    WiFi_Manager_Render();
}

static void pwd_key(char key)
{
    switch (key) {
    case '2':   /* UP: next character (an empty slot starts a new one) */
    case '8': { /* DOWN: previous character */
        int dir = (key == '2') ? 1 : -1;
        if (pwd_focus != 0) { pwd_focus = 0; break; }
        if (pwd_cursor >= pwd_len) {
            if (pwd_len >= edit_max()) break;
            pwd_buffer[pwd_len] = next_char('\0', dir);
            pwd_len++;
            pwd_buffer[pwd_len] = '\0';
        } else {
            pwd_buffer[pwd_cursor] = next_char(pwd_buffer[pwd_cursor], dir);
        }
        break;
    }
    case '4':   /* LEFT */
        if (pwd_focus == 0) {
            if (pwd_cursor > 0) pwd_cursor--; else pwd_focus = 2;
        } else if (pwd_focus == 1) {
            pwd_focus = 0; pwd_cursor = pwd_len;
        } else {
            pwd_focus = 1;
        }
        break;
    case '6':   /* RIGHT */
        if (pwd_focus == 0) {
            if (pwd_cursor < pwd_len) pwd_cursor++; else pwd_focus = 1;
        } else if (pwd_focus == 1) {
            pwd_focus = 2;
        } else {
            pwd_focus = 0; pwd_cursor = 0;
        }
        break;
    case '*':   /* GEAR: backspace */
        if (pwd_focus != 0) { pwd_focus = 0; break; }
        if (pwd_cursor > 0) {
            memmove(&pwd_buffer[pwd_cursor - 1], &pwd_buffer[pwd_cursor], (size_t)(pwd_len - pwd_cursor + 1U));
            pwd_cursor--;
            pwd_len--;
        }
        break;
    case '#':   /* PLAY / OK */
        if (pwd_focus == 0) {
            if (pwd_cursor < pwd_len) pwd_cursor++; else pwd_focus = 1;   /* past the last char = done typing */
        } else if (pwd_focus == 1) {
            pwd_do_connect();
            return;
        } else {
            set_system_state(STATE_WIFI_APP);
            return;
        }
        break;
    default:
        break;
    }
    if (pwd_cursor > pwd_len) pwd_cursor = pwd_len;
    pwd_fix_view();
}

/* ------------------------------------------------------------------------------------------ */
/*  Screens                                                                                    */
/* ------------------------------------------------------------------------------------------ */
static void WiFi_DrawSignalBars(uint8_t x, uint8_t y, int8_t rssi)
{
    uint8_t bars = 1;
    if (rssi >= -55) bars = 4;
    else if (rssi >= -68) bars = 3;
    else if (rssi >= -80) bars = 2;

    for (uint8_t b = 0; b < 4; b++) {
        uint8_t barH = 2 + (b * 2);
        uint8_t barX = x + (b * 3);
        uint8_t barY = y + (8 - barH);
        if (b < bars) {
            sh1106_drawVLine(barX, barY, barH, SH1106_WHITE);
            sh1106_drawVLine(barX + 1, barY, barH, SH1106_WHITE);
        } else {
            sh1106_drawPixel(barX, y + 7, SH1106_WHITE);
        }
    }
}

static void draw_button(uint8_t x, uint8_t w, const char *label, bool focused)
{
    sh1106_drawRect(x, 50, w, 13, SH1106_WHITE);
    if (focused) sh1106_drawRect((uint8_t)(x - 1), 49, (uint8_t)(w + 2), 15, SH1106_WHITE);
    uint8_t tw = (uint8_t)(6U * strlen(label) - 1U);
    sh1106_drawString((uint8_t)(x + (w - tw) / 2U), 53, label, 1, SH1106_WHITE);
}

/* little marks used by the lists: filled square = connected now, check = saved */
static void draw_net_mark(uint8_t x, uint8_t y, bool active)
{
    if (active) {
        sh1106_fillRect(x, (uint8_t)(y + 1), 5, 5, SH1106_WHITE);
    } else {
        sh1106_drawPixel(x, (uint8_t)(y + 3), SH1106_WHITE);
        sh1106_drawPixel((uint8_t)(x + 1), (uint8_t)(y + 4), SH1106_WHITE);
        sh1106_drawPixel((uint8_t)(x + 2), (uint8_t)(y + 3), SH1106_WHITE);
        sh1106_drawPixel((uint8_t)(x + 3), (uint8_t)(y + 2), SH1106_WHITE);
        sh1106_drawPixel((uint8_t)(x + 4), (uint8_t)(y + 1), SH1106_WHITE);
    }
}

static bool is_active_ssid(const char *ssid) { return wifi_connected && strcmp(esp01s_active_ssid, ssid) == 0; }

/* ---- Saved networks page ---- */
#define SV_ROWS 4
static uint8_t sv_sel = 0, sv_top = 0;
static bool    sv_back = false;

static void draw_back_box(bool focused, const char *hint)
{
    sh1106_drawRect(2, 51, 30, 12, SH1106_WHITE);
    if (focused) sh1106_drawRect(1, 49, 32, 15, SH1106_WHITE);
    sh1106_drawString(5, 54, "BACK", 1, SH1106_WHITE);
    if (hint) sh1106_drawString(38, 54, hint, 1, SH1106_WHITE);
}

static void render_saved(void)
{
    uint8_t cnt = WStore_Count();
    uint8_t rows = (uint8_t)(cnt + 1U);                         /* + the "ADD NETWORK" row */
    sh1106_drawString(4, 2, "SAVED WIFI", 1, SH1106_WHITE);
    char c[12];
    snprintf(c, sizeof(c), "%u/%u", (unsigned)cnt, (unsigned)WSTORE_MAX);
    sh1106_drawString((uint8_t)(124 - 6 * (uint8_t)strlen(c)), 2, c, 1, SH1106_WHITE);
    sh1106_drawHLine(0, 11, 128, SH1106_WHITE);

    for (uint8_t i = 0; i < SV_ROWS; i++) {
        uint8_t idx = (uint8_t)(sv_top + i);
        if (idx >= rows) break;
        uint8_t y = (uint8_t)(15 + 9 * i);
        if (idx < cnt) {
            const wstore_net_t *n = WStore_Get(idx);
            char nm[20];
            snprintf(nm, sizeof(nm), "%.13s", n->ssid);
            sh1106_drawString(6, y, nm, 1, SH1106_WHITE);
            if (is_active_ssid(n->ssid)) draw_net_mark(95, y, true);
            int8_t r;
            if (scan_rssi(n->ssid, &r)) WiFi_DrawSignalBars(110, y, r);
        } else {
            sh1106_drawString(6, y, "+ ADD NETWORK", 1, SH1106_WHITE);
        }
        if (!sv_back && idx == sv_sel) sh1106_drawRect(2, (uint8_t)(y - 1), 124, 9, SH1106_WHITE);
    }
    draw_back_box(sv_back, sv_back ? NULL : (sv_sel < cnt ? "OK:OPTIONS" : "OK:ADD"));
    if (cnt == 0) {
        sh1106_drawString(6, 26, "NOTHING SAVED YET", 1, SH1106_WHITE);
        sh1106_drawString(6, 34, "A GOOD CONNECTION", 1, SH1106_WHITE);
        sh1106_drawString(6, 42, "IS SAVED BY ITSELF", 1, SH1106_WHITE);
    }
}

/* ---- One network: connect / edit password / forget / disconnect ---- */
typedef enum { ACT_CONNECT = 0, ACT_EDIT, ACT_FORGET, ACT_DISC } wact_t;
static char    act_ssid[ESP01S_MAX_SSID_LEN + 1] = "";
static wact_t  act_ids[3];
static uint8_t act_n = 0, act_sel = 0;
static bool    act_back = false, act_confirm = false;
static wview_t act_return = V_SAVED;

static void act_build(void)
{
    act_n = 0;
    act_ids[act_n++] = is_active_ssid(act_ssid) ? ACT_DISC : ACT_CONNECT;
    act_ids[act_n++] = ACT_EDIT;
    act_ids[act_n++] = ACT_FORGET;
}

static void open_action(const char *ssid, wview_t from)
{
    snprintf(act_ssid, sizeof(act_ssid), "%s", ssid);
    act_return = from;
    act_sel = 0; act_back = false; act_confirm = false;
    act_build();
    view = V_ACTION;
}

static void render_action(void)
{
    bool active = is_active_ssid(act_ssid);
    char t[20];
    snprintf(t, sizeof(t), "%.13s", act_ssid);
    sh1106_drawString(4, 2, t, 1, SH1106_WHITE);
    if (active) sh1106_drawString(92, 2, "ONLINE", 1, SH1106_WHITE);
    else        sh1106_drawString(96, 2, "SAVED", 1, SH1106_WHITE);
    sh1106_drawHLine(0, 11, 128, SH1106_WHITE);

    char info[24];
    if (esp01s_status_msg[0] != '\0' && strcmp(esp01s_status_msg, "ONLINE") != 0 && (HAL_GetTick() - status_tick) < 6000U)
        snprintf(info, sizeof(info), "%.20s", esp01s_status_msg);
    else if (active)
        snprintf(info, sizeof(info), "IP:%.17s", esp01s_ip_address);
    else {
        int8_t r;
        snprintf(info, sizeof(info), "%s", scan_rssi(act_ssid, &r) ? "IN RANGE" : "NOT IN RANGE");
    }
    sh1106_drawString(4, 14, info, 1, SH1106_WHITE);
    sh1106_drawHLine(0, 22, 128, SH1106_WHITE);

    static const char *const NAMES[] = { "CONNECT", "EDIT PASSWORD", "FORGET", "DISCONNECT" };
    for (uint8_t i = 0; i < act_n; i++) {
        uint8_t y = (uint8_t)(24 + 9 * i);
        const char *txt = NAMES[act_ids[i]];
        if (act_ids[i] == ACT_FORGET && act_confirm) txt = "FORGET? PRESS OK";
        sh1106_drawString(6, y, txt, 1, SH1106_WHITE);
        if (!act_back && i == act_sel) sh1106_drawRect(2, (uint8_t)(y - 1), 124, 9, SH1106_WHITE);
    }
    draw_back_box(act_back, NULL);
}

static void WiFi_Manager_Render(void)
{
    sh1106_clear();

    /* ---- password screen ---- */
    if (current_state == STATE_WIFI_PASSWORD) {
        char buf[32];
        if (edit_mode) snprintf(buf, sizeof(buf), "NETWORK NAME");
        else           snprintf(buf, sizeof(buf), "%.13s", target_ssid);
        sh1106_drawString(4, 2, buf, 1, SH1106_WHITE);
        snprintf(buf, sizeof(buf), "%u/%u", (unsigned)pwd_len, (unsigned)edit_max());
        sh1106_drawString((uint8_t)(128 - 4 - 6 * (uint8_t)strlen(buf)), 2, buf, 1, SH1106_WHITE);
        sh1106_drawHLine(0, 12, 128, SH1106_WHITE);

        for (uint8_t i = 0; i < 16; i++) {
            uint8_t slotX = (uint8_t)(8 + i * 6);
            uint8_t idx = (uint8_t)(pwd_view + i);
            if (idx < pwd_len) sh1106_drawChar(slotX, 20, pwd_buffer[idx], 1, SH1106_WHITE);
            sh1106_drawLine(slotX, 30, (uint8_t)(slotX + 4), 30, SH1106_WHITE);
        }
        if (pwd_view > 0) sh1106_drawChar(0, 20, '<', 1, SH1106_WHITE);
        if (pwd_view + 16U < pwd_len) sh1106_drawChar(122, 20, '>', 1, SH1106_WHITE);

        if (pwd_focus == 0 && pwd_cursor >= pwd_view && pwd_cursor < pwd_view + 16U) {
            uint8_t cursorX = (uint8_t)(8 + (pwd_cursor - pwd_view) * 6);
            sh1106_drawHLine((uint8_t)(cursorX - 1), 31, 7, SH1106_WHITE);
            sh1106_drawHLine((uint8_t)(cursorX - 1), 32, 7, SH1106_WHITE);
        }

        if (esp01s_status_msg[0] != '\0') {
            snprintf(buf, sizeof(buf), "%.20s", esp01s_status_msg);
            sh1106_drawString(4, 36, buf, 1, SH1106_WHITE);
        } else {
            sh1106_drawString(4, 36, "UP/DN:CHAR  *:DEL", 1, SH1106_WHITE);
        }

        sh1106_drawRect(10, 48, 56, 14, SH1106_WHITE);
        if (pwd_focus == 1) sh1106_drawRect(8, 46, 60, 18, SH1106_WHITE);
        if (edit_mode) sh1106_drawString(29, 51, "NEXT", 1, SH1106_WHITE);
        else           sh1106_drawString(17, 51, "CONNECT", 1, SH1106_WHITE);

        sh1106_drawRect(78, 48, 40, 14, SH1106_WHITE);
        if (pwd_focus == 2) sh1106_drawRect(76, 46, 44, 18, SH1106_WHITE);
        sh1106_drawString(85, 51, "BACK", 1, SH1106_WHITE);

        sh1106_update();
        return;
    }

    /* ---- saved networks / one network ---- */
    if (view == V_SAVED)  { render_saved();  sh1106_update(); return; }
    if (view == V_ACTION) { render_action(); sh1106_update(); return; }

    /* ---- network list screen ---- */
    sh1106_drawString(4, 2, "WIFI", 1, SH1106_WHITE);
    if (wifi_connected) {
        sh1106_drawString(92, 2, "ONLINE", 1, SH1106_WHITE);
    } else {
        sh1106_drawString(86, 2, "OFFLINE", 1, SH1106_WHITE);
    }
    sh1106_drawHLine(0, 12, 128, SH1106_WHITE);

    char banner[32];
    if (wifi_connected && !(esp01s_status_msg[0] != '\0' && strcmp(esp01s_status_msg, "ONLINE") != 0 &&
                            (HAL_GetTick() - status_tick) < 6000U)) {
        snprintf(banner, sizeof(banner), "IP:%.17s", esp01s_ip_address);
    } else {
        snprintf(banner, sizeof(banner), "%.20s", esp01s_status_msg);
    }
    sh1106_drawString(4, 14, banner, 1, SH1106_WHITE);
    sh1106_drawHLine(0, 23, 128, SH1106_WHITE);

    const uint8_t startY = 25;
    const uint8_t maxLines = 2;

    if (esp01s_scanned_count == 0) {
        sh1106_drawString(14, 29, "PRESS SCAN TO FIND", 1, SH1106_WHITE);
        sh1106_drawString(14, 38, "NETWORKS", 1, SH1106_WHITE);
    }

    for (uint8_t i = 0; i < maxLines; i++) {
        uint8_t itemIdx = (uint8_t)(wifi_scroll_offset + i);
        if (itemIdx >= esp01s_scanned_count) break;

        uint8_t yPos = (uint8_t)(startY + i * 11);
        WiFi_AP_Entry_t *ap = &esp01s_scanned_aps[itemIdx];

        if (wifi_footer_focus == 0 && itemIdx == wifi_selected_index) {
            sh1106_drawRect(2, (uint8_t)(yPos - 1), 124, 11, SH1106_WHITE);
        }

        if (ap->encryption > 0) {
            sh1106_drawPixel(6, (uint8_t)(yPos + 2), SH1106_WHITE);
            sh1106_drawPixel(7, (uint8_t)(yPos + 1), SH1106_WHITE);
            sh1106_drawPixel(8, (uint8_t)(yPos + 2), SH1106_WHITE);
            sh1106_drawRect(5, (uint8_t)(yPos + 3), 5, 4, SH1106_WHITE);
        }

        char dispBuf[20];
        snprintf(dispBuf, sizeof(dispBuf), "%.13s", ap->ssid);
        sh1106_drawString(14, (uint8_t)(yPos + 1), dispBuf, 1, SH1106_WHITE);
        WiFi_DrawSignalBars(112, (uint8_t)(yPos + 1), ap->rssi);
        if (WStore_Find(ap->ssid) >= 0) draw_net_mark(94, yPos, is_active_ssid(ap->ssid));
    }

    if (wifi_scroll_offset > 0) {
        draw_8x8_icon(100, startY + 1, icon_uparrow_8x8);
    }
    if (wifi_scroll_offset + maxLines < esp01s_scanned_count) {
        draw_8x8_icon(100, (uint8_t)(startY + 11 + 1), icon_downarrow_8x8);
    }

    sh1106_drawHLine(0, 48, 128, SH1106_WHITE);
    draw_button(2,  28, "SCAN",  wifi_footer_focus == 1);
    draw_button(32, 36, "SAVED", wifi_footer_focus == 2);
    draw_button(70, 28, "SYNC",  wifi_footer_focus == 3);
    draw_button(99, 27, "BACK",  wifi_footer_focus == 4);

    sh1106_update();
}

/* Connect to a saved network with its stored password; a refused password opens the editor. */
static void connect_saved(const char *ssid)
{
    user_disconnected = false;
    show_busy("CONNECTING...");
    if (join_saved(ssid)) { view = V_LIST; return; }
    if (esp01s_state == ESP01S_STATE_ERR_WRONG_PWD) {
        pwd_open(ssid);
        set_status("WRONG PASSWORD");
    }
}

static void do_scan(void)
{
    show_busy("SCANNING...");
    if (ESP01S_ScanNetworks()) { last_scan_tick = HAL_GetTick(); scanned_once = true; }
}

static void saved_key(char key)
{
    uint8_t cnt = WStore_Count();
    uint8_t rows = (uint8_t)(cnt + 1U);
    if (sv_sel >= rows) sv_sel = (uint8_t)(rows - 1U);
    if (key == '2') {
        if (sv_back) { sv_back = false; sv_sel = (uint8_t)(rows - 1U); }
        else if (sv_sel > 0) sv_sel--;
    } else if (key == '8') {
        if (sv_back) { sv_back = false; sv_sel = 0; }
        else if (sv_sel + 1U < rows) sv_sel++;
        else sv_back = true;
    } else if (key == '*') {
        sv_back = !sv_back;
    } else if (key == '4') {
        view = V_LIST;
        return;
    } else if (key == '#' || key == '6') {
        if (sv_back) { if (key == '#') view = V_LIST; return; }
        if (sv_sel < cnt) {
            char nm[ESP01S_MAX_SSID_LEN + 1];
            snprintf(nm, sizeof(nm), "%s", WStore_Get(sv_sel)->ssid);
            open_action(nm, V_SAVED);
        } else {
            name_open();
        }
        return;
    }
    if (sv_sel < sv_top) sv_top = sv_sel;
    if (sv_sel >= sv_top + SV_ROWS) sv_top = (uint8_t)(sv_sel - SV_ROWS + 1U);
}

static void action_key(char key)
{
    if (key == '2') {
        act_confirm = false;
        if (act_back) { act_back = false; act_sel = (uint8_t)(act_n - 1U); }
        else if (act_sel > 0) act_sel--;
    } else if (key == '8') {
        act_confirm = false;
        if (act_back) { act_back = false; act_sel = 0; }
        else if (act_sel + 1U < act_n) act_sel++;
        else act_back = true;
    } else if (key == '*') {
        act_back = !act_back; act_confirm = false;
    } else if (key == '4') {
        view = act_return;
    } else if (key == '#' || key == '6') {
        if (act_back) { if (key == '#') view = act_return; return; }
        switch (act_ids[act_sel]) {
        case ACT_CONNECT:
            connect_saved(act_ssid);
            if (view == V_LIST || current_state != STATE_WIFI_APP) return;
            act_build();
            break;
        case ACT_EDIT:
            pwd_open(act_ssid);
            break;
        case ACT_FORGET:
            if (!act_confirm) { act_confirm = true; break; }
            if (is_active_ssid(act_ssid)) { ESP01S_Disconnect(); user_disconnected = true; }
            WStore_Forget(act_ssid);
            sync_saved_compat();
            if (strcmp(skip_ssid, act_ssid) == 0) skip_ssid[0] = '\0';
            set_status("FORGOTTEN");
            view = (act_return == V_SAVED) ? V_SAVED : V_LIST;
            if (view == V_SAVED && sv_sel > WStore_Count()) sv_sel = WStore_Count();
            return;
        case ACT_DISC:
            show_busy("DISCONNECTING...");
            ESP01S_Disconnect();
            user_disconnected = true;                    /* stay offline until a network is chosen again */
            act_build();
            break;
        }
    }
}

void WiFi_Manager_Process(char key, uint32_t current_tick)
{
    /* Re-entering the app (called every pass while it is open, so a long gap means it was closed in between) */
    bool just_entered = (current_state == STATE_WIFI_APP) && ((HAL_GetTick() - wifi_timer) > 2500U);
    if (just_entered) {
        view = V_LIST;
        wifi_footer_focus = 0;
    }

    if (just_entered && key == 0 && module_ok && view == V_LIST && !is_alarm_ringing &&
        (!scanned_once || (HAL_GetTick() - last_scan_tick) > 60000UL)) {
        do_scan();                                          /* like a phone: look around as soon as the screen opens */
        wifi_timer = HAL_GetTick();
        WiFi_Manager_Render();
        return;
    }

    if (key != 0) {
        /* ---- password / name screen ---- */
        if (current_state == STATE_WIFI_PASSWORD) {
            pwd_key(key);
            wifi_timer = HAL_GetTick();
            WiFi_Manager_Render();
            return;
        }

        if (view == V_SAVED)       saved_key(key);
        else if (view == V_ACTION) action_key(key);
        else {
        /* ---- network list screen ---- */
        const uint8_t maxLines = 2;

        if (key == '2') {
            if (wifi_footer_focus > 0) {
                wifi_footer_focus = 0;
            } else if (wifi_selected_index > 0) {
                wifi_selected_index--;
                if (wifi_selected_index < wifi_scroll_offset) wifi_scroll_offset = wifi_selected_index;
            }
        } else if (key == '8') {
            if (wifi_footer_focus == 0) {
                if (wifi_selected_index + 1 < esp01s_scanned_count) {
                    wifi_selected_index++;
                    if (wifi_selected_index >= wifi_scroll_offset + maxLines) {
                        wifi_scroll_offset = (uint8_t)(wifi_selected_index - maxLines + 1);
                    }
                } else {
                    wifi_footer_focus = 1;
                }
            }
        } else if (key == '4') {
            wifi_footer_focus = (wifi_footer_focus == 0) ? 4 : (uint8_t)(wifi_footer_focus - 1);
        } else if (key == '6') {
            wifi_footer_focus = (wifi_footer_focus >= 4) ? 0 : (uint8_t)(wifi_footer_focus + 1);
        } else if (key == '*') {
            wifi_footer_focus = (wifi_footer_focus == 4) ? 0 : 4;
        } else if (key == '#') {
            if (wifi_footer_focus == 4) {
                set_system_state(STATE_ICON_MENU);
                return;
            } else if (wifi_footer_focus == 1) {
                do_scan();
            } else if (wifi_footer_focus == 2) {
                sv_sel = 0; sv_top = 0; sv_back = false;
                view = V_SAVED;
            } else if (wifi_footer_focus == 3) {
                if (wifi_connected) {
                    show_busy("SYNCING TIME...");
                    ESP01S_SyncTime();
                } else {
                    set_status("NEED WIFI FIRST");
                }
            } else if (wifi_footer_focus == 0 && esp01s_scanned_count > 0) {
                WiFi_AP_Entry_t *ap = &esp01s_scanned_aps[wifi_selected_index];
                char nm[ESP01S_MAX_SSID_LEN + 1];
                snprintf(nm, sizeof(nm), "%s", ap->ssid);
                if (is_active_ssid(nm)) {
                    open_action(nm, V_LIST);                      /* the network you are on: disconnect / forget */
                } else if (WStore_Find(nm) >= 0) {
                    connect_saved(nm);                            /* known: no password needed */
                } else if (ap->encryption == 0) {
                    snprintf(target_ssid, sizeof(target_ssid), "%s", nm);
                    user_disconnected = false;
                    show_busy("CONNECTING...");
                    bool ok = ESP01S_ConnectAP(nm, "");           /* open network */
                    note_join_result(nm, ok);
                    if (ok) WiFi_Creds_Save(nm, "");
                } else {
                    pwd_open(nm);
                }
            }
        }
        }
        wifi_timer = HAL_GetTick();
        WiFi_Manager_Render();
        return;
    }

    if ((current_tick - wifi_timer) >= 50) {
        wifi_timer = current_tick;
        WiFi_Manager_Render();
    }
}
