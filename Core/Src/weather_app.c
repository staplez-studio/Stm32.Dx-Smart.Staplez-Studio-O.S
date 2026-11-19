/* Live weather over Wi-Fi: one plain HTTP request to wttr.in (see wifi_config.h). Nothing is shown
 * unless it was really fetched. */
#include "weather_app.h"
#include "staplez_studio.h"
#include "wifi_manager.h"
#include "wifi_config.h"
#include "wifi_net.h"
#include "sh1106.h"
#include "ui_draw.h"
#include "weather_icons.h"
#include <stdio.h>
#include <string.h>

static uint32_t w_timer = 0;
static wnet_weather_t wx;                  /* last good reading */
static bool     wx_have = false;
static uint32_t wx_tick = 0;               /* when it was fetched */
static uint8_t  wx_h = 12, wx_m = 0, wx_pm = 0;   /* local time of that fetch, for display */
static char     wx_status[24] = "";

static bool Weather_Fetch(void)
{
    if (!wifi_connected) {
        snprintf(wx_status, sizeof(wx_status), "WIFI: NOT CONNECTED");
        return false;
    }
    static char resp[768];
    char req[220];
    snprintf(req, sizeof(req),
             "GET /%s?format=%%t;%%h;%%C;%%l%s HTTP/1.0\r\n"
             "Host: %s\r\nUser-Agent: curl/8.0\r\nAccept: */*\r\n\r\n",
             WEATHER_LOCATION, WEATHER_FAHRENHEIT ? "&u" : "&m", WEATHER_HOST);

    uint16_t n = ESP01S_HttpGet(WEATHER_HOST, 80, req, resp, sizeof(resp), 10000);
    if (n == 0) {
        snprintf(wx_status, sizeof(wx_status), "NO RESPONSE");
        return false;
    }
    if (wnet_http_status(resp) != 200) {
        snprintf(wx_status, sizeof(wx_status), "SERVER ERROR %d", wnet_http_status(resp));
        return false;
    }
    wnet_weather_t tmp;
    if (!wnet_parse_weather(wnet_http_body(resp), &tmp)) {
        snprintf(wx_status, sizeof(wx_status), "BAD REPLY");
        return false;
    }
    wx = tmp;
    wx_have = true;
    wx_tick = HAL_GetTick();
    wx_h = current_hour; wx_m = current_minute; wx_pm = is_pm;
    wx_status[0] = '\0';
    return true;
}

static void draw_cloud(uint8_t x, uint8_t y)
{
    sh1106_fillCircle(x + 4, y + 8, 3, SH1106_WHITE);
    sh1106_fillCircle(x + 8, y + 6, 4, SH1106_WHITE);
    sh1106_fillCircle(x + 11, y + 8, 3, SH1106_WHITE);
    sh1106_fillRect(x + 4, y + 8, 8, 3, SH1106_WHITE);
}

void Weather_DrawIcon16(uint8_t x, uint8_t y, wnet_icon_t k);
static void draw_wx_icon(uint8_t x, uint8_t y, wnet_icon_t k)
{
    switch (k) {
    case WNET_ICON_SUN:
        draw_16x16_icon(x, y, weather_sunny_16);
        break;
    case WNET_ICON_RAIN:
        draw_cloud(x, y);
        sh1106_drawLine(x + 5, y + 12, x + 4, y + 15, SH1106_WHITE);
        sh1106_drawLine(x + 9, y + 12, x + 8, y + 15, SH1106_WHITE);
        sh1106_drawLine(x + 13, y + 12, x + 12, y + 15, SH1106_WHITE);
        break;
    case WNET_ICON_SNOW:
        draw_cloud(x, y);
        sh1106_drawPixel(x + 5, y + 13, SH1106_WHITE);
        sh1106_drawPixel(x + 9, y + 15, SH1106_WHITE);
        sh1106_drawPixel(x + 13, y + 13, SH1106_WHITE);
        break;
    case WNET_ICON_STORM:
        draw_cloud(x, y);
        sh1106_drawLine(x + 9, y + 11, x + 6, y + 14, SH1106_BLACK);
        sh1106_drawLine(x + 10, y + 11, x + 7, y + 15, SH1106_WHITE);
        sh1106_drawLine(x + 7, y + 15, x + 11, y + 15, SH1106_WHITE);
        break;
    case WNET_ICON_FOG:
        sh1106_drawHLine(x + 1, y + 4, 14, SH1106_WHITE);
        sh1106_drawHLine(x + 3, y + 8, 12, SH1106_WHITE);
        sh1106_drawHLine(x + 1, y + 12, 14, SH1106_WHITE);
        break;
    default:
        draw_cloud(x, y);
        break;
    }
}

void Weather_DrawIcon16(uint8_t x, uint8_t y, wnet_icon_t k)
{
    draw_wx_icon(x, y, k);
}

bool Weather_GetCurrent(wnet_icon_t *icon, int16_t *temp)
{
    if (!wifi_connected || !wx_have) return false;
    if ((HAL_GetTick() - wx_tick) > 3UL * 3600UL * 1000UL) return false;   /* stale: fall back to sun / moon */
    if (icon) *icon = wnet_icon_for(wx.cond);
    if (temp) *temp = wx.temp;
    return true;
}

static void Weather_Render(void)
{
    char buf[32];
    sh1106_clear();
    sh1106_drawString(2, 2, "WEATHER", 1, SH1106_WHITE);
    if (wx_have) draw_wx_icon(108, 0, wnet_icon_for(wx.cond));
    sh1106_drawHLine(0, 12, wx_have ? 104 : 128, SH1106_WHITE);   /* leave the icon uncut */

    if (wx_have) {
        snprintf(buf, sizeof(buf), "%.20s", wx.loc[0] ? wx.loc : "YOUR LOCATION");
        sh1106_drawString(4, 16, buf, 1, SH1106_WHITE);
        snprintf(buf, sizeof(buf), "TEMP:%d*%c  HUM:%u%%", (int)wx.temp, WEATHER_FAHRENHEIT ? 'F' : 'C', (unsigned)wx.humidity);
        sh1106_drawString(4, 27, buf, 1, SH1106_WHITE);
        snprintf(buf, sizeof(buf), "%.20s", wx.cond);
        sh1106_drawString(4, 38, buf, 1, SH1106_WHITE);
    } else {
        sh1106_drawString(4, 18, "NO WEATHER YET", 1, SH1106_WHITE);
        sh1106_drawString(4, 30, wifi_connected ? "PLAY: GET WEATHER" : "CONNECT WIFI FIRST", 1, SH1106_WHITE);
    }

    sh1106_drawHLine(0, 49, 128, SH1106_WHITE);
    if (wx_status[0]) {
        snprintf(buf, sizeof(buf), "%.20s", wx_status);
    } else if (wx_have) {
        snprintf(buf, sizeof(buf), "%u:%02u%c  PLAY:UPDATE", (unsigned)wx_h, (unsigned)wx_m, wx_pm ? 'P' : 'A');
    } else {
        snprintf(buf, sizeof(buf), "*:BACK");
    }
    sh1106_drawString(4, 54, buf, 1, SH1106_WHITE);
    sh1106_update();
}

static uint32_t wx_next_try = 0;
static bool     wx_armed = false;

/* Automatic refresh while online: when first connected, then every WEATHER_REFRESH_MIN. A failed attempt
 * waits 15 minutes so an unreachable server does not stall the clock again and again. */
void Weather_Background(uint32_t current_tick)
{
    if (!wifi_connected) { wx_armed = false; return; }
    if (!wx_armed) { wx_armed = true; wx_next_try = current_tick; }
    if ((int32_t)(current_tick - wx_next_try) < 0) return;

    sh1106_clear();
    sh1106_drawString(4, 20, "WEATHER", 1, SH1106_WHITE);
    sh1106_drawString(4, 32, "UPDATING...", 1, SH1106_WHITE);
    sh1106_update();

    bool ok = Weather_Fetch();
    wx_next_try = current_tick + (ok ? (uint32_t)WEATHER_REFRESH_MIN * 60000UL : 15UL * 60000UL);
}

static void do_update(void)
{
    snprintf(wx_status, sizeof(wx_status), "UPDATING...");
    Weather_Render();
    Weather_Fetch();
}

void Weather_App_Process(char key, uint32_t current_tick)
{
    bool just_opened = (current_tick - w_timer) > 2000U;   /* the screen is redrawn every 50 ms while open */

    if (key == '*') {
        set_system_state(STATE_ICON_MENU);
        return;
    }

    if (just_opened && wifi_connected) {
        uint32_t age_ms = HAL_GetTick() - wx_tick;
        if (!wx_have || age_ms > (uint32_t)WEATHER_REFRESH_MIN * 60000UL) {
            do_update();
        }
    }

    if (key == '#') {
        do_update();
        w_timer = current_tick;
        Weather_Render();
        return;
    }

    if (just_opened || (current_tick - w_timer) >= 50) {
        w_timer = current_tick;
        Weather_Render();
    }
}
