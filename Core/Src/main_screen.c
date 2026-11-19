/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    main_screen.c
  * @brief   Main Standby Screen matching emulator renderMainScreen to a tee.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "main_screen.h"
#include "staplez_studio.h"
#include "sh1106.h"
#include "ui_draw.h"
#include "weather_icons.h"
#include "weather_app.h"
#include "wifi_config.h"
#include "time_manager.h"
#include "wifi_manager.h"
#include "ir_remote_app.h"
#include "alarm_manager.h"
#include "fan_app.h"
#include <stdio.h>
#include <string.h>

static uint8_t settings_gear_selected = 0;
static uint32_t last_clock_render = 0;

void Main_Screen_Init(void)
{
    settings_gear_selected = 0;
    last_clock_render = 0;
}

void Main_Screen_Render(void)
{
    sh1106_clear();

    // 1. Top Title: "STAPLEZ STUDIO %u" (e.g. STAPLEZ STUDIO 2026) at (4, 2)
    char topBuf[28];
    snprintf(topBuf, sizeof(topBuf), "STAPLEZ STUDIO %u", current_year);
    sh1106_drawString(4, 2, topBuf, 1, SH1106_WHITE);

    // 2. Large Time HH:MM (scale 2)
    char hhBuf[4], mmBuf[4], secMsBuf[12];
    snprintf(hhBuf, sizeof(hhBuf), "%02u", current_hour);
    snprintf(mmBuf, sizeof(mmBuf), "%02u", current_minute);
    snprintf(secMsBuf, sizeof(secMsBuf), "%02u.%u", current_second, (current_ms / 100));

    sh1106_drawString(2, 16, hhBuf, 2, SH1106_WHITE);

    // Blinking colon every 500ms (tick % 1000 < 500)
    if ((HAL_GetTick() / 500) % 2 == 0) {
        sh1106_drawString(26, 16, ":", 2, SH1106_WHITE);
    }

    sh1106_drawString(35, 16, mmBuf, 2, SH1106_WHITE);

    // AM/PM and seconds.tenths
    sh1106_drawString(65, 16, is_pm ? "PM" : "AM", 1, SH1106_WHITE);
    sh1106_drawString(65, 24, secMsBuf, 1, SH1106_WHITE);

    // 3. Day / Night Weather Icon (top right x=95, y=16)
    uint8_t is_night = ((is_pm && current_hour >= 6 && current_hour != 12) || (!is_pm && (current_hour == 12 || current_hour < 6)));
    /* Online with a recent reading: show the real weather. Offline, stale, or clear sky: just the sun / moon. */
    wnet_icon_t wicon = WNET_ICON_SUN;
    int16_t wtemp = 0;
    bool have_wx = Weather_GetCurrent(&wicon, &wtemp);
    if (have_wx && wicon != WNET_ICON_SUN) {
        char tb[8];
        snprintf(tb, sizeof(tb), "%d", (int)wtemp);
        Weather_DrawIcon16(90, 16, wicon);
        sh1106_drawString((uint8_t)(127 - 6 * (uint8_t)strlen(tb)), 20, tb, 1, SH1106_WHITE);
    } else if (is_night) {
        draw_long_32x16_icon(95, 16, weather_moon_left, weather_moon_right);
    } else {
        draw_long_32x16_icon(95, 16, weather_sunny_left, weather_sunny_right);
    }

    // 4. Date line: MONTH DAY_NUM WDAY (e.g. OCT 19 MON) at (2, 34)
    char dateBuf[24];
    const char *m_name = (current_month >= 1 && current_month <= 12) ? months[current_month] : "JAN";
    const char *w_name = (current_wday < 7) ? week_days[current_wday] : "MON";
    snprintf(dateBuf, sizeof(dateBuf), "%s %02u %s", m_name, current_day_num, w_name);
    sh1106_drawString(2, 34, dateBuf, 1, SH1106_WHITE);

    // 5. Bottom status bar (y=48):
    // Battery icon (x=2, y=48)
    draw_long_32x16_icon(2, 48, noBat_left, noBat_right);

    // WiFi signal icon (x=34, y=48)
    if (wifi_connected) {
        draw_16x16_icon(34, 48, sc16_wifi_signal);
    }

    // Daylight saving in effect (the time zone and rules are set under Set Time > zone)
    if (tz_dst_now) {
        sh1106_drawString(74, 52, "DST", 1, SH1106_WHITE);
    }

    // Alarm note icon (x=52, y=48)
    if (Any_Alarm_Enabled()) {
        uint8_t rhythm2 = (alarms[0].enabled && alarms[0].rhythm_type == 2);
        draw_16x16_icon(52, 48, rhythm2 ? sc16_double_music_note : sc16_music_note);
    }

    // Live Received IR Hex code (x=73, y=34)
    const char *ir_hex = IR_Get_Last_Hex_Str();
    if (!ir_hex) ir_hex = "0x7158";
    char ir_short[8];
    if (strstr(ir_hex, "F2F0") != NULL) {
        snprintf(ir_short, sizeof(ir_short), "0xF2F0");
    } else if (strstr(ir_hex, "F2E0") != NULL) {
        snprintf(ir_short, sizeof(ir_short), "0xF2E0");
    } else if (strstr(ir_hex, "72D8") != NULL) {
        snprintf(ir_short, sizeof(ir_short), "0x72D8");
    } else if (strstr(ir_hex, "72C8") != NULL) {
        snprintf(ir_short, sizeof(ir_short), "0x72C8");
    } else if (strstr(ir_hex, "7158") != NULL) {
        snprintf(ir_short, sizeof(ir_short), "0x7158");
    } else if (strstr(ir_hex, "7198") != NULL) {
        snprintf(ir_short, sizeof(ir_short), "0x7198");
    } else {
        snprintf(ir_short, sizeof(ir_short), "%.6s", ir_hex);
    }
    sh1106_drawString(73, 34, ir_short, 1, SH1106_WHITE);

    // Settings Gear Icon (x=108, y=45)
    draw_16x16_icon(108, 45, sc16_gear);
    if (settings_gear_selected) {
        sh1106_drawRect(105, 42, 22, 22, SH1106_WHITE);
    }

    sh1106_update();
}

uint8_t Main_Screen_Is_Gear_Selected(void) {
    return settings_gear_selected;
}

void Main_Screen_Set_Gear_Selected(uint8_t sel) {
    settings_gear_selected = sel;
}

void Main_Screen_Process(char key, uint32_t current_tick)
{
    (void)key;
    // Refresh home clock at 20 FPS (every 50ms)
    if ((current_tick - last_clock_render) >= 50) {
        last_clock_render = current_tick;
        Main_Screen_Render();
    }
}
