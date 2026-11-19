/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    alarm_manager.c
  * @brief   Alarm Manager UI matching emulator renderAlarmTimeApp,
  *          renderAlarmSingleApp, and renderAlarmDoubleApp to a tee.
  *          Controlled 100% by IR Remote (Left, Right, Up, Down, Play, Shift).
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "alarm_manager.h"
#include "sound_app.h"
#include "staplez_studio.h"
#include "sh1106.h"
#include "ui_draw.h"
#include "weather_icons.h"
#include <stdio.h>
#include <string.h>

uint8_t is_sdcard_inserted = 1;
uint8_t is_alarm_test_playing = 0;

AlarmConfig_t alarms[1] = {
    {12, 0, 0, 1, 0, 0, 1} // Daily Alarm: 12:00 AM, Single tone, Sheet Music, Disabled by default, MP3 track 1
};

static uint8_t alarm_time_focus = 1;   // 1: Hour, 2: Min, 3: AMPM, 4: TEST, ...
static uint8_t alarm_single_focus = 0; // 0..4: Melodies, 5: Back
static uint8_t alarm_single_top = 0;   // first melody shown in the 3-row window
static uint16_t alarm_double_focus = 0; // 0..n-1: MP3 tracks the DFPlayer reported, n: Back
static uint16_t alarm_double_top = 0;   // first track shown in the 3-row window
static uint32_t alarm_timer = 0;

void Alarm_Manager_Init(void)
{
    alarm_time_focus = 1;
    alarm_single_focus = 0;
    alarm_single_top = 0;
    alarm_double_focus = 0;
    alarm_double_top = 0;
    alarm_timer = 0;
    is_alarm_test_playing = 0;
}

uint8_t Any_Alarm_Enabled(void)
{
    return alarms[0].enabled ? 1 : 0;
}

void EEPROM_LoadAlarms(void)
{
    DS3231_LoadAllSettings();
}

void EEPROM_SaveAlarms(void)
{
    DS3231_SaveAllSettings();
}

void Alarm_StopTestMelody(void)
{
    is_alarm_test_playing = 0;
    Buzzer_Stop();
}

void Alarm_ToggleTestMelody(void)
{
    if (is_alarm_test_playing) {
        Alarm_StopTestMelody();
    } else {
        is_alarm_test_playing = 1;
    }
}

uint8_t is_alarm_ringing = 0;
static uint8_t dfplayer_alarm_started = 0;   /* MP3 alarm: track command already sent for this ring */
static uint32_t last_alarm_trigger_minute = 0xFFFFFFFF;
static uint32_t alarm_ring_start_tick = 0;

void Alarm_Dismiss(void)
{
    if (is_alarm_ringing) {
        is_alarm_ringing = 0;
        Buzzer_Stop();
        ActiveBuzzer_Off();
        if (dfplayer_alarm_started) {   /* the MP3 keeps playing unless told to stop */
            dfplayer_alarm_started = 0;
            DFPlayer_Pause();
        }
    }
}

void Alarm_Check_Process(uint32_t current_tick)
{
    if (!alarms[0].enabled) {
        if (is_alarm_ringing) Alarm_Dismiss();
        return;
    }

    uint32_t current_total_min = ((uint32_t)current_hour * 60) + current_minute + (is_pm ? 720 : 0);
    uint32_t alarm_total_min = ((uint32_t)alarms[0].hour * 60) + alarms[0].minute + (alarms[0].is_pm ? 720 : 0);

    // Trigger when current minute matches alarm minute
    if (current_total_min == alarm_total_min) {
        if (last_alarm_trigger_minute != current_total_min) {
            last_alarm_trigger_minute = current_total_min;
            is_alarm_ringing = 1;
            alarm_ring_start_tick = current_tick;
        }
    } else {
        last_alarm_trigger_minute = 0xFFFFFFFF; // Reset trigger lock for next cycle
    }

    if (is_alarm_ringing) {
        // Auto shutoff after 60 seconds (60000ms) if not dismissed
        if ((current_tick - alarm_ring_start_tick) >= 60000) {
            Alarm_Dismiss();
            return;
        }

        // Active Ringing Mode (Non-blocking state machine!)
        if (alarms[0].rhythm_type == 1) { // Buzzer
            Alarm_Ringing_Process(current_tick);
        } else { // DFPlayer MP3
            // Send the command ONCE per ring. It is a ~10 ms bit-banged frame; repeating it on every
            // main-loop pass blocked the loop and made the DFPlayer restart the track continuously.
            if (!dfplayer_alarm_started) {
                dfplayer_alarm_started = 1;
                DFPlayer_PlayTrack(alarms[0].mp3_track ? alarms[0].mp3_track : 1);
            }
        }
    } else {
        Alarm_Ringing_Process(current_tick); // Resets sound state machine when disarmed
    }
}

void Alarm_PlayTestMelody(void)
{
    Alarm_ToggleTestMelody();
}

void Alarm_Manager_Render(void)
{
    sh1106_clear();

    if (current_state == STATE_ALARM_SINGLE_APP) {
        sh1106_drawString(4, 2, "BUZZER MELODIES", 1, SH1106_WHITE);
        sh1106_drawLine(0, 11, 127, 11, SH1106_WHITE);

        const char *melodies[5] = {
            "1.Basic Chime",
            "2.Standard Beep",
            "3.Popcorn Mel",
            "4.Wake Klaxon",
            "5.Active Pulse"
        };

        /* Three rows are shown at a time (the 5 melodies plus BACK do not fit on 64 px). The window follows
         * the focus; the small arrows at the right say that more rows are above or below. */
        if (alarm_single_focus < 5) {
            if (alarm_single_focus < alarm_single_top) alarm_single_top = alarm_single_focus;
            if (alarm_single_focus > alarm_single_top + 2U) alarm_single_top = (uint8_t)(alarm_single_focus - 2U);
        }
        for (uint8_t r = 0; r < 3; r++) {
            uint8_t i = (uint8_t)(alarm_single_top + r);
            uint8_t y = (uint8_t)(14 + r * 10);
            uint8_t isSelected = (alarms[0].buzzer_melody == i);
            char lineBuf[32];
            snprintf(lineBuf, sizeof(lineBuf), "[%c] %s", isSelected ? '*' : ' ', melodies[i]);
            sh1106_drawString(6, y, lineBuf, 1, SH1106_WHITE);
            if (alarm_single_focus == i) {
                sh1106_drawRect(3, y - 2, 122, 11, SH1106_WHITE);
            }
        }
        if (alarm_single_top > 0) {                         /* more above */
            sh1106_drawLine(119, 15, 119, 15, SH1106_WHITE);
            sh1106_drawLine(118, 16, 120, 16, SH1106_WHITE);
            sh1106_drawLine(117, 17, 121, 17, SH1106_WHITE);
        }
        if (alarm_single_top + 3U < 5U) {                   /* more below */
            sh1106_drawLine(117, 35, 121, 35, SH1106_WHITE);
            sh1106_drawLine(118, 36, 120, 36, SH1106_WHITE);
            sh1106_drawLine(119, 37, 119, 37, SH1106_WHITE);
        }

        sh1106_drawRect(4, 46, 34, 15, SH1106_WHITE);
        sh1106_drawString(9, 50, "back", 1, SH1106_WHITE);
        if (alarm_single_focus == 5) {
            sh1106_drawRect(2, 44, 38, 19, SH1106_WHITE);
        }

        sh1106_update();
        return;
    }

    if (current_state == STATE_ALARM_DOUBLE_APP) {
        /* Only what the DFPlayer itself reports is listed: the number of MP3 files on ITS micro SD card
         * (it cannot report names, so tracks are shown by number). */
        bool known = DFPlayer_TrackCountKnown() && DFPlayer_GetTrackCount() > 0;
        uint16_t n = known ? DFPlayer_GetTrackCount() : 1U;
        char title[24];
        if (known) snprintf(title, sizeof(title), "DFPLAYER: %u MP3", (unsigned)DFPlayer_GetTrackCount());
        else       snprintf(title, sizeof(title), "DFPLAYER SD CARD");
        sh1106_drawString(4, 2, title, 1, SH1106_WHITE);
        sh1106_drawLine(0, 11, 127, 11, SH1106_WHITE);

        if (known) {
            if (alarm_double_focus < n) {
                if (alarm_double_focus < alarm_double_top) alarm_double_top = alarm_double_focus;
                if (alarm_double_focus > alarm_double_top + 2U) alarm_double_top = (uint16_t)(alarm_double_focus - 2U);
            }
            for (uint8_t r = 0; r < 3; r++) {
                uint16_t t = (uint16_t)(alarm_double_top + r);
                if (t >= n) break;
                uint8_t y = (uint8_t)(14 + r * 10);
                char lineBuf[24];
                snprintf(lineBuf, sizeof(lineBuf), "[%c] TRACK %03u", (alarms[0].mp3_track == t + 1U) ? '*' : ' ', (unsigned)(t + 1U));
                sh1106_drawString(6, y, lineBuf, 1, SH1106_WHITE);
                if (alarm_double_focus == t) sh1106_drawRect(3, y - 2, 122, 11, SH1106_WHITE);
            }
            if (alarm_double_top > 0) {
                sh1106_drawLine(119, 15, 119, 15, SH1106_WHITE);
                sh1106_drawLine(118, 16, 120, 16, SH1106_WHITE);
                sh1106_drawLine(117, 17, 121, 17, SH1106_WHITE);
            }
            if ((uint16_t)(alarm_double_top + 3U) < n) {
                sh1106_drawLine(117, 35, 121, 35, SH1106_WHITE);
                sh1106_drawLine(118, 36, 120, 36, SH1106_WHITE);
                sh1106_drawLine(119, 37, 119, 37, SH1106_WHITE);
            }
        } else {
            sh1106_drawString(4, 15, DFPlayer_LastError(), 1, SH1106_WHITE);
            sh1106_drawString(4, 25, "DFP TX -> PA1", 1, SH1106_WHITE);
            sh1106_drawString(6, 37, "ASK AGAIN", 1, SH1106_WHITE);
            if (alarm_double_focus == 0) sh1106_drawRect(3, 35, 122, 11, SH1106_WHITE);
        }

        sh1106_drawRect(4, 46, 34, 15, SH1106_WHITE);
        sh1106_drawString(9, 50, "back", 1, SH1106_WHITE);
        if (alarm_double_focus == n) {
            sh1106_drawRect(2, 44, 38, 19, SH1106_WHITE);
        }

        sh1106_update();
        return;
    }

    // STATE_ALARM_TIME_APP
    sh1106_drawString(4, 2, "SET ALARM", 1, SH1106_WHITE);
    sh1106_drawLine(0, 12, 127, 12, SH1106_WHITE);
    sh1106_drawString(98, 2, alarms[0].enabled ? "ON" : "OFF", 1, SH1106_WHITE);

    char hourStr[4], minStr[4];
    snprintf(hourStr, sizeof(hourStr), "%02u", alarms[0].hour);
    snprintf(minStr, sizeof(minStr), "%02u", alarms[0].minute);

    // Row 1: Large Hour digits (scale 2)
    sh1106_drawString(4, 16, hourStr, 2, SH1106_WHITE);
    if (alarm_time_focus == 1) {
        sh1106_drawRect(2, 14, 28, 20, SH1106_WHITE);
    }

    // Blinking Colon (scale 2)
    if ((HAL_GetTick() / 500) % 2 == 0) {
        sh1106_drawString(32, 16, ":", 2, SH1106_WHITE);
    }

    // Large Minute digits (scale 2)
    sh1106_drawString(42, 16, minStr, 2, SH1106_WHITE);
    if (alarm_time_focus == 2) {
        sh1106_drawRect(40, 14, 28, 20, SH1106_WHITE);
    }

    // AM/PM Button Box
    sh1106_drawString(74, 20, alarms[0].is_pm ? "pm" : "am", 1, SH1106_WHITE);
    sh1106_drawRect(72, 18, 17, 13, SH1106_WHITE);
    if (alarm_time_focus == 3) {
        sh1106_drawRect(70, 16, 21, 17, SH1106_WHITE);
    }

    // TEST/STOP Button beside AM/PM (Focus 4)
    sh1106_drawString(95, 20, is_alarm_test_playing ? "STOP" : "TEST", 1, SH1106_WHITE);
    sh1106_drawRect(91, 18, 33, 13, SH1106_WHITE);
    if (alarm_time_focus == 4) {
        sh1106_drawRect(89, 16, 37, 17, SH1106_WHITE);
    }

    // Row 2: Bottom action buttons
    if (alarms[0].enabled) {
        // "off" button (Focus 5 when alarm enabled)
        sh1106_drawString(7, 50, "off", 1, SH1106_WHITE);
        sh1106_drawRect(3, 47, 24, 14, SH1106_WHITE);
        if (alarm_time_focus == 5) {
            sh1106_drawRect(1, 45, 28, 18, SH1106_WHITE);
        }
    }

    uint8_t single_focus = alarms[0].enabled ? 6 : 5;
    uint8_t double_focus = is_sdcard_inserted ? (alarms[0].enabled ? 7 : 6) : 0;
    uint8_t action_focus = is_sdcard_inserted ? (alarms[0].enabled ? 8 : 7) : (alarms[0].enabled ? 7 : 6);

    // Single Note Icon Button (Buzzer)
    uint8_t singleX = alarms[0].enabled ? (is_sdcard_inserted ? 34 : 45) : (is_sdcard_inserted ? 20 : 35);
    draw_16x16_icon(singleX, 45, sc16_music_note);
    sh1106_drawRect(singleX - 2, 44, 20, 18, SH1106_WHITE);
    if (alarm_time_focus == single_focus) {
        sh1106_drawRect(singleX - 4, 42, 24, 22, SH1106_WHITE);
    }
    if (alarms[0].enabled && alarms[0].rhythm_type == 1) {
        sh1106_drawLine(singleX - 1, 60, singleX + 17, 60, SH1106_WHITE);
    }

    // Double Note Icon Button (DFPlayer Mini SD Card) - Populates ONLY if SD card is inserted!
    if (is_sdcard_inserted) {
        uint8_t doubleX = alarms[0].enabled ? 58 : 50;
        draw_16x16_icon(doubleX, 45, sc16_double_music_note);
        sh1106_drawRect(doubleX - 2, 44, 20, 18, SH1106_WHITE);
        if (alarm_time_focus == double_focus) {
            sh1106_drawRect(doubleX - 4, 42, 24, 22, SH1106_WHITE);
        }
        if (alarms[0].enabled && alarms[0].rhythm_type == 2) {
            sh1106_drawLine(doubleX - 1, 60, doubleX + 17, 60, SH1106_WHITE);
        }
    }

    // Dynamic Action Button: 'set' if alarm is active/set to go off, 'back' if turned off
    const char *btnLabel = alarms[0].enabled ? "set" : "back";
    uint8_t boxW = alarms[0].enabled ? 32 : 36;
    uint8_t actX = 128 - boxW - 3;
    sh1106_drawString(actX + (alarms[0].enabled ? 6 : 5), 49, btnLabel, 1, SH1106_WHITE);
    sh1106_drawRect(actX, 46, boxW, 16, SH1106_WHITE);
    if (alarm_time_focus == action_focus) {
        sh1106_drawRect(actX - 2, 44, boxW + 4, 20, SH1106_WHITE);
    }

    sh1106_update();
}

void Alarm_Manager_Process(char key, uint32_t current_tick)
{
    if (key != 0) {
        if (current_state == STATE_ALARM_SINGLE_APP) {
            Alarm_StopTestMelody();
            if (key == '2') { // UP
                alarm_single_focus = (alarm_single_focus == 0) ? 5 : alarm_single_focus - 1;
            } else if (key == '8') { // DOWN
                alarm_single_focus = (alarm_single_focus + 1) % 6;
            } else if (key == '4' || key == '6' || key == '*') {
                alarm_single_focus = (alarm_single_focus == 5) ? 0 : 5;
            } else if (key == '#' || key == '5' || key == 'C') {
                if (alarm_single_focus < 5) {
                    alarms[0].buzzer_melody = alarm_single_focus;
                    alarms[0].rhythm_type = 1;
                    alarms[0].enabled = 1;
                    EEPROM_SaveSettings();
                    Alarm_PlayTestMelody();
                }
                alarm_time_focus = alarms[0].enabled ? 6 : 5;
                set_system_state(STATE_ALARM_TIME_APP);
            }
            Alarm_Manager_Render();
            return;
        }

        if (current_state == STATE_ALARM_DOUBLE_APP) {
            Alarm_StopTestMelody();
            bool known = DFPlayer_TrackCountKnown() && DFPlayer_GetTrackCount() > 0;
            uint16_t n = known ? DFPlayer_GetTrackCount() : 1U;          /* index n = BACK */
            if (key == '2') {
                alarm_double_focus = (alarm_double_focus == 0) ? n : (uint16_t)(alarm_double_focus - 1U);
            } else if (key == '8') {
                alarm_double_focus = (alarm_double_focus >= n) ? 0 : (uint16_t)(alarm_double_focus + 1U);
            } else if (key == '4' || key == '6' || key == '*') {
                alarm_double_focus = (alarm_double_focus == n) ? 0 : n;
            } else if (key == '#' || key == '5' || key == 'C') {
                if (alarm_double_focus < n && !known) {                 /* "ASK AGAIN" */
                    DFPlayer_RefreshTrackCount();
                    alarm_double_focus = 0;
                    Alarm_Manager_Render();
                    return;
                }
                if (alarm_double_focus < n) {
                    alarms[0].mp3_track = (uint8_t)((alarm_double_focus + 1U) > 255U ? 255U : (alarm_double_focus + 1U));
                    alarms[0].enabled = 1;
                    alarms[0].rhythm_type = 2;
                    EEPROM_SaveSettings();
                }
                alarm_time_focus = is_sdcard_inserted ? (alarms[0].enabled ? 7 : 6) : (alarms[0].enabled ? 6 : 5);
                set_system_state(STATE_ALARM_TIME_APP);
            }
            Alarm_Manager_Render();
            return;
        }

        // STATE_ALARM_TIME_APP
        uint8_t single_focus = alarms[0].enabled ? 6 : 5;
        uint8_t double_focus = is_sdcard_inserted ? (alarms[0].enabled ? 7 : 6) : 0;
        uint8_t action_focus = is_sdcard_inserted ? (alarms[0].enabled ? 8 : 7) : (alarms[0].enabled ? 7 : 6);
        uint8_t max_focus = action_focus;

        uint8_t old_focus = alarm_time_focus;

        if (key == '4' || key == 'B') { // LEFT
            alarm_time_focus = (alarm_time_focus <= 1) ? max_focus : alarm_time_focus - 1;
        } else if (key == '6' || key == 'A') { // RIGHT
            alarm_time_focus = (alarm_time_focus >= max_focus) ? 1 : alarm_time_focus + 1;
        } else if (key == '2') { // UP
            if (alarm_time_focus == 1) { alarms[0].hour = (alarms[0].hour % 12) + 1; }
            else if (alarm_time_focus == 2) { alarms[0].minute = (alarms[0].minute + 1) % 60; }
            else if (alarm_time_focus == 3) { alarms[0].is_pm = !alarms[0].is_pm; }
            else if (alarm_time_focus == 4) { Alarm_ToggleTestMelody(); }
            else if (alarm_time_focus == 5 && alarms[0].enabled) { alarms[0].enabled = 0; EEPROM_SaveSettings(); }
            else if (alarm_time_focus == single_focus) { alarms[0].enabled = 1; alarms[0].rhythm_type = 1; EEPROM_SaveSettings(); }
            else if (double_focus > 0 && alarm_time_focus == double_focus) { alarms[0].enabled = 1; alarms[0].rhythm_type = 2; EEPROM_SaveSettings(); }
            else { alarm_time_focus = 1; }
        } else if (key == '8') { // DOWN
            if (alarm_time_focus == 1) { alarms[0].hour = (alarms[0].hour <= 1) ? 12 : alarms[0].hour - 1; }
            else if (alarm_time_focus == 2) { alarms[0].minute = (alarms[0].minute == 0) ? 59 : alarms[0].minute - 1; }
            else if (alarm_time_focus == 3) { alarms[0].is_pm = !alarms[0].is_pm; }
            else if (alarm_time_focus == 4) { Alarm_ToggleTestMelody(); }
            else if (alarm_time_focus == 5 && alarms[0].enabled) { alarms[0].enabled = 0; EEPROM_SaveSettings(); }
            else if (alarm_time_focus == single_focus) { alarms[0].enabled = 1; alarms[0].rhythm_type = 1; EEPROM_SaveSettings(); }
            else if (double_focus > 0 && alarm_time_focus == double_focus) { alarms[0].enabled = 1; alarms[0].rhythm_type = 2; EEPROM_SaveSettings(); }
        } else if (key == '*') { // SHIFT / GEAR
            if (alarm_time_focus != action_focus) {
                alarm_time_focus = action_focus;
            } else {
                alarm_time_focus = 1;
            }
        } else if (key == '#' || key == '5' || key == 'C') { // PLAY / ENTER
            if (alarm_time_focus == 1) {
                alarm_time_focus = 2;
            } else if (alarm_time_focus == 2) {
                alarm_time_focus = 3;
            } else if (alarm_time_focus == 3) {
                alarms[0].is_pm = !alarms[0].is_pm;
            } else if (alarm_time_focus == 4) {
                Alarm_ToggleTestMelody();
            } else if (alarm_time_focus == 5 && alarms[0].enabled) {
                alarms[0].enabled = 0;
                EEPROM_SaveSettings();
            } else if (alarm_time_focus == single_focus) {
                Alarm_StopTestMelody();
                alarms[0].enabled = 1;
                alarms[0].rhythm_type = 1;
                alarm_single_focus = alarms[0].buzzer_melody;
                set_system_state(STATE_ALARM_SINGLE_APP);
                Alarm_Manager_Render();
                return;
            } else if (double_focus > 0 && alarm_time_focus == double_focus) {
                Alarm_StopTestMelody();
                alarms[0].enabled = 1;
                alarms[0].rhythm_type = 2;
                DFPlayer_RefreshTrackCount();                 /* real list: what the DFPlayer's own card holds */
                alarm_double_focus = (DFPlayer_TrackCountKnown() && alarms[0].mp3_track >= 1 &&
                                      alarms[0].mp3_track <= DFPlayer_GetTrackCount()) ? (uint16_t)(alarms[0].mp3_track - 1U) : 0;
                set_system_state(STATE_ALARM_DOUBLE_APP);
                Alarm_Manager_Render();
                return;
            } else if (alarm_time_focus == action_focus) {
                Alarm_StopTestMelody();
                EEPROM_SaveSettings();
                set_system_state(STATE_ICON_MENU);
                return;
            }
        }

        // Navigating off focus 4 stops the test loop
        if (old_focus == 4 && alarm_time_focus != 4) {
            Alarm_StopTestMelody();
        }

        Alarm_Manager_Render();
        return;
    }

    if (alarm_time_focus != 4 || current_state != STATE_ALARM_TIME_APP) {
        if (is_alarm_test_playing) {
            Alarm_StopTestMelody();
        }
    } else if (is_alarm_test_playing) {
        Alarm_Test_Melody_Process();
    }

    if ((current_tick - alarm_timer) >= 50) {
        alarm_timer = current_tick;
        Alarm_Manager_Render();
    }
}
