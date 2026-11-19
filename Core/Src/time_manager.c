/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    time_manager.c
  * @brief   Interactive RTC Time and Date configuration UI with isolated edit buffers.
  *          Zero I2C calls while cycling Up/Down for 100% glitch-free performance.
  *          Commits to DS3231 and EEPROM only when pressing SET.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "time_manager.h"
#include "staplez_studio.h"
#include "sh1106.h"
#include "ui_draw.h"
#include "timezone_app.h"
#include <stdio.h>
#include <stdbool.h>

bool time_is_set = true;
static uint8_t time_focus = 0; // 0=Hour, 1=Min, 2=AMPM, 3=Month, 4=Day, 5=Year, 6=Set, 7=Cancel, 8=Zone
static uint8_t last_time_focus = 0;
static uint32_t time_timer = 0;

/* Local editing buffer: completely disconnected from live ticking clock while setting */
static uint8_t edit_hour = 12;
static uint8_t edit_minute = 0;
static uint8_t edit_is_pm = 0;
static uint8_t edit_month = 10;
static uint8_t edit_day = 24;
static uint16_t edit_year = 2026;
static bool edit_loaded = false;

static uint8_t get_days_in_month(uint16_t y, uint8_t m)
{
    if (m == 2) {
        bool leap = ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
        return leap ? 29 : 28;
    }
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    return 31;
}

void Time_Manager_Enter(void)
{
    // Snapshot current live time into isolated edit buffers
    edit_hour = (current_hour >= 1 && current_hour <= 12) ? current_hour : 12;
    edit_minute = (current_minute < 60) ? current_minute : 0;
    edit_is_pm = is_pm ? 1 : 0;
    edit_month = (current_month >= 1 && current_month <= 12) ? current_month : 10;
    edit_day = (current_day_num >= 1 && current_day_num <= 31) ? current_day_num : 24;
    edit_year = (current_year >= 2020 && current_year <= 2099) ? current_year : 2026;
    time_focus = 0;
    last_time_focus = 0;
    edit_loaded = true;
}

static void Time_Manager_Render(uint32_t current_tick)
{
    (void)current_tick;
    sh1106_clear();

    sh1106_drawString(4, 2, "set time & date", 1, SH1106_WHITE);
    sh1106_drawLine(0, 12, 127, 12, SH1106_WHITE);

    char hourStr[4], minStr[4];
    snprintf(hourStr, sizeof(hourStr), "%02u", edit_hour);
    snprintf(minStr, sizeof(minStr), "%02u", edit_minute);

    // Hour digits
    sh1106_drawString(4, 16, hourStr, 2, SH1106_WHITE);
    if (time_focus == 0) {
        sh1106_drawRect(2, 14, 28, 20, SH1106_WHITE);
    }

    // Steady colon (clock is paused while setting)
    sh1106_drawString(32, 16, ":", 2, SH1106_WHITE);

    // Minute digits
    sh1106_drawString(42, 16, minStr, 2, SH1106_WHITE);
    if (time_focus == 1) {
        sh1106_drawRect(40, 14, 28, 20, SH1106_WHITE);
    }

    // AM/PM
    sh1106_drawString(74, 20, edit_is_pm ? "pm" : "am", 1, SH1106_WHITE);
    if (time_focus == 2) {
        sh1106_drawRect(72, 18, 24, 14, SH1106_WHITE);
    }

    // Date layout: MM/DD/YYYY
    char mmStr[4], ddStr[4], yyStr[8];
    snprintf(mmStr, sizeof(mmStr), "%02u", edit_month);
    snprintf(ddStr, sizeof(ddStr), "%02u", edit_day);
    snprintf(yyStr, sizeof(yyStr), "%04u", edit_year);

    sh1106_drawString(16, 36, mmStr, 1, SH1106_WHITE);
    sh1106_drawString(31, 36, "/", 1, SH1106_WHITE);
    sh1106_drawString(40, 36, ddStr, 1, SH1106_WHITE);
    sh1106_drawString(55, 36, "/", 1, SH1106_WHITE);
    sh1106_drawString(66, 36, yyStr, 1, SH1106_WHITE);

    if (time_focus == 3) sh1106_drawRect(14, 34, 16, 12, SH1106_WHITE);
    if (time_focus == 4) sh1106_drawRect(38, 34, 16, 12, SH1106_WHITE);
    if (time_focus == 5) sh1106_drawRect(62, 34, 32, 12, SH1106_WHITE);

    // SET button
    sh1106_drawString(7, 50, "set", 1, SH1106_WHITE);
    sh1106_drawRect(4, 48, 26, 12, SH1106_WHITE);
    if (time_focus == 6) {
        sh1106_drawRect(2, 46, 30, 16, SH1106_WHITE);
    }

    // ZONE button (time zone and daylight saving)
    sh1106_drawString(40, 50, "zone", 1, SH1106_WHITE);
    sh1106_drawRect(36, 48, 34, 12, SH1106_WHITE);
    if (time_focus == 8) {
        sh1106_drawRect(34, 46, 38, 16, SH1106_WHITE);
    }

    // CANCEL button
    sh1106_drawString(82, 50, "cancel", 1, SH1106_WHITE);
    sh1106_drawRect(78, 48, 44, 12, SH1106_WHITE);
    if (time_focus == 7) {
        sh1106_drawRect(76, 46, 48, 16, SH1106_WHITE);
    }

    sh1106_update();
}

void Time_Manager_Process(char key, uint32_t current_tick)
{
    if (!edit_loaded) {
        Time_Manager_Enter();
    }

    if (key != 0) {
        if (key == '4') {
            // IR LEFT (0x72C8)
            if (time_focus == 0) time_focus = 7;
            else if (time_focus == 7) time_focus = 8;
            else if (time_focus == 8) time_focus = 6;
            else time_focus--;
            if (time_focus < 7) last_time_focus = time_focus;
        } else if (key == '6') {
            // IR RIGHT (0x72D8)
            if (time_focus == 6) time_focus = 8;
            else if (time_focus == 8) time_focus = 7;
            else if (time_focus == 7) time_focus = 0;
            else time_focus++;
            if (time_focus < 7) last_time_focus = time_focus;
        } else if (key == '2') {
            // IR UP (0xF2F0) - FAST RAM UPDATE ONLY (NO I2C writes while editing!)
            if (time_focus == 0) { edit_hour = (edit_hour % 12) + 1; }
            else if (time_focus == 1) { edit_minute = (edit_minute + 1) % 60; }
            else if (time_focus == 2) { edit_is_pm = !edit_is_pm; }
            else if (time_focus == 3) { edit_month = (edit_month % 12) + 1; }
            else if (time_focus == 4) {
                uint8_t maxD = get_days_in_month(edit_year, edit_month);
                edit_day = (edit_day >= maxD) ? 1 : edit_day + 1;
            }
            else if (time_focus == 5) { if (edit_year < 2099) edit_year++; }
            else if (time_focus == 7 || time_focus == 8) { time_focus = (last_time_focus < 7) ? last_time_focus : 0; }
        } else if (key == '8') {
            // IR DOWN (0xF2E0) - FAST RAM UPDATE ONLY (NO I2C writes while editing!)
            if (time_focus == 0) { edit_hour = (edit_hour <= 1) ? 12 : edit_hour - 1; }
            else if (time_focus == 1) { edit_minute = (edit_minute == 0) ? 59 : edit_minute - 1; }
            else if (time_focus == 2) { edit_is_pm = !edit_is_pm; }
            else if (time_focus == 3) { edit_month = (edit_month <= 1) ? 12 : edit_month - 1; }
            else if (time_focus == 4) {
                uint8_t maxD = get_days_in_month(edit_year, edit_month);
                edit_day = (edit_day <= 1) ? maxD : edit_day - 1;
            }
            else if (time_focus == 5) { if (edit_year > 2020) edit_year--; }
        } else if (key == '#') {
            // IR PLAY (0x7158)
            if (time_focus == 8) {
                // ZONE: open the time zone screen (the edits here are kept)
                Timezone_App_Enter();
                set_system_state(STATE_TIMEZONE_APP);
                return;
            } else if (time_focus == 7) {
                // CANCEL: Discard edits and return to menu without touching DS3231
                edit_loaded = false;
                set_system_state(STATE_ICON_MENU);
                return;
            } else {
                // COMMIT & SAVE: Write once to DS3231 hardware RTC and EEPROM
                current_hour = edit_hour;
                current_minute = edit_minute;
                current_second = 0;
                is_pm = edit_is_pm;
                current_month = edit_month;
                current_day_num = edit_day;
                current_year = edit_year;
                current_wday = calculate_day_of_week(current_year, current_month, current_day_num);

                DS3231_SetTime(current_hour, current_minute, current_second, is_pm, current_day_num, current_month, current_year);
                EEPROM_SaveSettings();

                time_is_set = true;
                edit_loaded = false;
                set_system_state(STATE_MAIN_SCREEN);
                return;
            }
        } else if (key == '*') {
            // IR SHIFT (0x7198): toggle between fields and CANCEL (focus 7)
            if (time_focus == 7 || time_focus == 8) {
                time_focus = (last_time_focus < 7) ? last_time_focus : 0;
            } else {
                last_time_focus = time_focus;
                time_focus = 7;
            }
        }
        time_timer = current_tick;
        Time_Manager_Render(current_tick);
        return;
    }

    if ((current_tick - time_timer) >= 50) {
        time_timer = current_tick;
        Time_Manager_Render(current_tick);
    }
}
