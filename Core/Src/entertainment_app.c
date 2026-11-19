/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    entertainment_app.c
  * @brief   Entertainment Application UI matching emulator renderEntertainmentApp.
  *          Controlled 100% by IR Remote (Left, Right, Up, Down, Play, Shift).
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "entertainment_app.h"
#include "staplez_studio.h"
#include "sound_app.h"
#include "sh1106.h"
#include "ui_draw.h"
#include "weather_icons.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define NUM_ENT_ITEMS 4
static uint8_t ent_selected_index = 0; // 0..3 apps, 4 BACK button
static uint8_t ent_sub_view = 0;       // 0: menu, 1: (unused: files moved to the Hard Drive app), 2: mp3, 3: screen shift, 4: rfid tool
static uint8_t rfid_focus_index = 0;   // 0: Read, 1: Write/Clone, 2: Pinout Diagram, 3: Logs, 4: Back
static uint32_t ent_timer = 0;
static bool mp3_playing = false;
static uint16_t mp3_track = 1;          // DFPlayer track number (1..count)

static char rfid_tag_id[16] = "0008432190";
static char rfid_tag_hex[16] = "0080B83E";
static char rfid_write_id[16] = "0008432190";
static char rfid_status_msg[32] = "RDM6300 READY (125kHz)";

void Entertainment_App_Init(void)
{
    ent_selected_index = 0;
    ent_sub_view = 0;
    rfid_focus_index = 0;
    ent_timer = 0;
    mp3_playing = false;
}

void Entertainment_App_Render(void)
{
    sh1106_clear();

    if (ent_sub_view == 0) {
        // Matching emulator renderEntertainmentApp:
        sh1106_drawString(4, 4, "ENTERTAINMENT", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 14, 128, SH1106_WHITE);

        const uint16_t *ent_icons[4] = {
            folder_icon,
            mp3_player_icon,
            sc16_screen_calibrator,
            sc16_rfid_card
        };

        uint8_t iconY = 20;

        // Left Arrow [<] at (2, 20)
        draw_16x16_icon(2, iconY, sc16_leftarrow);

        uint8_t startX = 20;
        uint8_t spacing = 22;
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t curX = startX + (i * spacing);
            draw_16x16_icon(curX, iconY, ent_icons[i]);

            if (ent_selected_index < 4 && i == ent_selected_index) {
                sh1106_drawRect(curX - 2, iconY - 2, 20, 20, SH1106_WHITE);
            }
        }

        // Right Arrow [>] at (110, 20)
        draw_16x16_icon(110, iconY, sc16_rightarrow);

        // Bottom label
        if (ent_selected_index == 0) {
            sh1106_drawString(34, 50, "HARD DRIVE", 1, SH1106_WHITE);
        } else if (ent_selected_index == 1) {
            sh1106_drawString(36, 50, "MP3 PLAYER", 1, SH1106_WHITE);
        } else if (ent_selected_index == 2) {
            sh1106_drawString(36, 50, "SCREEN SHIFT", 1, SH1106_WHITE);
        } else if (ent_selected_index == 3) {
            sh1106_drawString(36, 50, "READ RFID", 1, SH1106_WHITE);
        } else if (ent_selected_index == 4) {
            sh1106_drawString(40, 50, "MAIN MENU", 1, SH1106_WHITE);
        }

        // Permanent BACK button box at bottom (x=4, y=48, w=28, h=12)
        sh1106_drawRect(4, 48, 28, 12, SH1106_WHITE);
        if (ent_selected_index == 4) {
            sh1106_drawRect(2, 46, 32, 16, SH1106_WHITE);
        }
        sh1106_drawString(6, 50, "BACK", 1, SH1106_WHITE);

    } else if (ent_sub_view == 2) {
        // MP3 Player subview: plays the files on the DFPlayer's OWN micro SD card. The module reports only how
        // many MP3 files it can read, so tracks are shown by number.
        sh1106_drawString(4, 2, "MP3 PLAYER", 1, SH1106_WHITE);
        sh1106_drawRect(98, 1, 26, 9, SH1106_WHITE);
        sh1106_drawString(102, 2, "EQ", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 11, 128, SH1106_WHITE);
        draw_16x16_icon(6, 16, mp3_player_icon);
        bool known = DFPlayer_TrackCountKnown() && DFPlayer_GetTrackCount() > 0;
        sh1106_drawString(28, 16, mp3_playing ? "STATE: PLAYING" : "STATE: PAUSED", 1, SH1106_WHITE);
        char tb[24];
        if (known) snprintf(tb, sizeof(tb), "TRACK %03u/%03u", (unsigned)mp3_track, (unsigned)DFPlayer_GetTrackCount());
        else       snprintf(tb, sizeof(tb), "TRACK ---/---");
        sh1106_drawString(28, 26, tb, 1, SH1106_WHITE);
        char vol_buf[32];
        snprintf(vol_buf, sizeof(vol_buf), "VOL:%2d/13", Sound_App_GetVolume());
        sh1106_drawString(4, 35, vol_buf, 1, SH1106_WHITE);
        if (known) {
            snprintf(tb, sizeof(tb), "%u MP3 ON SD CARD", (unsigned)DFPlayer_GetTrackCount());
            sh1106_drawString(4, 42, tb, 1, SH1106_WHITE);
        } else {
            sh1106_drawString(4, 42, DFPlayer_LastError(), 1, SH1106_WHITE);
        }
        sh1106_drawHLine(0, 50, 128, SH1106_WHITE);
        sh1106_drawString(4, 53, "L/R:TRACK PLAY:PAUSE", 1, SH1106_WHITE);

    } else if (ent_sub_view == 3) {
        // Screen Shift Calibrator subview
        sh1106_drawString(4, 2, "SCREEN CALIBRATOR", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 12, 128, SH1106_WHITE);
        char buf[32];
        snprintf(buf, sizeof(buf), "RAM OFFSET: %d px", sh1106_column_offset);
        sh1106_drawString(12, 22, buf, 1, SH1106_WHITE);
        sh1106_drawString(12, 34, "L:-1px  R:+1px", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 50, 128, SH1106_WHITE);
        sh1106_drawString(4, 53, "SHIFT:BACK PLAY:SAVE", 1, SH1106_WHITE);

    } else if (ent_sub_view == 4) {
        // RFID RDM6300 Read RFID Sub-App
        sh1106_drawString(2, 2, "READ RFID", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 11, 128, SH1106_WHITE);

        if (rfid_focus_index == 2) {
            // Pinout Diagram
            sh1106_drawString(4, 14, "RDM6300 WIRING:", 1, SH1106_WHITE);
            sh1106_drawString(4, 25, "VCC>+5V  GND>GND", 1, SH1106_WHITE);
            sh1106_drawString(4, 35, "TXD>PB11 (USART3)", 1, SH1106_WHITE);
            sh1106_drawString(4, 45, "RXD>NC  COIL>ANT1/2", 1, SH1106_WHITE);
            sh1106_drawHLine(0, 53, 128, SH1106_WHITE);
            sh1106_drawString(4, 55, "GEAR:BACK PLAY:MENU", 1, SH1106_WHITE);
        } else if (rfid_focus_index == 3) {
            // Logs
            sh1106_drawString(4, 14, "RECENT TAG LOGS:", 1, SH1106_WHITE);
            sh1106_drawString(4, 25, "#1 0008432190 80B83E", 1, SH1106_WHITE);
            sh1106_drawString(4, 35, "#2 0014283921 DA10A9", 1, SH1106_WHITE);
            sh1106_drawString(4, 45, "#3 0005128033 4E3E21", 1, SH1106_WHITE);
            sh1106_drawHLine(0, 53, 128, SH1106_WHITE);
            sh1106_drawString(4, 55, "GEAR:BACK PLAY:MENU", 1, SH1106_WHITE);
        } else {
            // Main Read Controls
            char readBuf[48];
            snprintf(readBuf, sizeof(readBuf), "TAG ID: %s", rfid_tag_id);
            sh1106_drawString(4, 14, readBuf, 1, SH1106_WHITE);
            if (rfid_focus_index == 0) {
                sh1106_drawRect(2, 12, 124, 11, SH1106_WHITE);
            }

            char formatBuf[48];
            snprintf(formatBuf, sizeof(formatBuf), "HEX:%s EM4100", rfid_tag_hex);
            sh1106_drawString(4, 25, formatBuf, 1, SH1106_WHITE);
            if (rfid_focus_index == 1) {
                sh1106_drawRect(2, 23, 124, 11, SH1106_WHITE);
            }

            sh1106_drawString(4, 36, "READ-ONLY SCANNER", 1, SH1106_WHITE);

            // Bottom action buttons: [BACK] [PINOUT] [LOGS]
            sh1106_drawRect(2, 49, 30, 12, SH1106_WHITE);
            if (rfid_focus_index == 4) {
                sh1106_drawRect(1, 47, 33, 16, SH1106_WHITE);
            }
            sh1106_drawString(5, 51, "BACK", 1, SH1106_WHITE);

            sh1106_drawRect(36, 49, 44, 12, SH1106_WHITE);
            if (rfid_focus_index == 2) {
                sh1106_drawRect(34, 47, 48, 16, SH1106_WHITE);
            }
            sh1106_drawString(40, 51, "PINOUT", 1, SH1106_WHITE);

            sh1106_drawRect(84, 49, 42, 12, SH1106_WHITE);
            if (rfid_focus_index == 3) {
                sh1106_drawRect(82, 47, 46, 16, SH1106_WHITE);
            }
            sh1106_drawString(89, 51, "LOGS", 1, SH1106_WHITE);
        }
    }

    sh1106_update();
}

void Entertainment_App_Process(char key, uint32_t current_tick)
{
    if (key != 0) {
        if (ent_sub_view == 0) {
            if (key == '4' || key == '2') {
                ent_selected_index = (ent_selected_index == 0) ? 4 : ent_selected_index - 1;
            } else if (key == '6' || key == '8') {
                ent_selected_index = (ent_selected_index + 1) % 5;
            } else if (key == '*') {
                ent_selected_index = (ent_selected_index == 4) ? 0 : 4;
            } else if (key == '5' || key == 'C' || key == '#') {
                if (ent_selected_index == 4) {
                    set_system_state(STATE_ICON_MENU);
                    return;
                }
                if (ent_selected_index == 0) {              // HARD DRIVE: the ESP32-CAM's SD card (names only)
                    set_system_state(STATE_FILE_EXPLORER_APP);
                    return;
                }
                ent_sub_view = ent_selected_index + 1;
                if (ent_sub_view == 2) {
                    Sound_Apply_Hardware_Force();
                    DFPlayer_RefreshTrackCount();           // ask the DFPlayer what its own card holds
                    mp3_track = 1;
                    mp3_playing = false;
                    if (DFPlayer_TrackCountKnown() && DFPlayer_GetTrackCount() > 0) {
                        DFPlayer_PlayTrack((uint8_t)mp3_track);
                        mp3_playing = true;
                    }
                }
            }
        } else {
            if (key == 'B' || key == '*') {
                if (ent_sub_view == 2) {
                    DFPlayer_Pause();
                    mp3_playing = false;
                }
                ent_sub_view = 0;
            } else if (ent_sub_view == 2) {
                if (key == '#') {
                    if (mp3_playing) {
                        DFPlayer_Pause();
                        mp3_playing = false;
                    } else {
                        DFPlayer_Play();
                        mp3_playing = true;
                    }
                } else if ((key == '4' || key == '6') && DFPlayer_TrackCountKnown() && DFPlayer_GetTrackCount() > 0) {
                    uint16_t n = DFPlayer_GetTrackCount();     // LEFT / RIGHT: previous / next track (wraps)
                    mp3_track = (key == '6') ? (uint16_t)((mp3_track % n) + 1U) : (uint16_t)((mp3_track <= 1U) ? n : mp3_track - 1U);
                    DFPlayer_PlayTrack((uint8_t)(mp3_track > 255U ? 255U : mp3_track));
                    mp3_playing = true;
                } else if (key == '2') {   // UP. (Digit buttons never reach this screen: they are global shortcuts.)
                    // Jump to Sound & Equalizer Settings while music continues playing; BACK returns here
                    set_system_state(STATE_SOUND_APP);
                    return;
                }
            } else if (ent_sub_view == 3) {
                if ((key == '4' || key == '8') && sh1106_column_offset > 0) {
                    sh1106_set_column_offset(sh1106_column_offset - 1);
                } else if ((key == '6' || key == '2') && sh1106_column_offset < 4) {
                    sh1106_set_column_offset(sh1106_column_offset + 1);
                } else if (key == '#') {
                    EEPROM_SaveSettings();
                    ent_sub_view = 0;
                }
            } else if (ent_sub_view == 4) {
                if (key == '2') { // UP
                    rfid_focus_index = (rfid_focus_index == 0) ? 4 : rfid_focus_index - 1;
                } else if (key == '8') { // DOWN
                    rfid_focus_index = (rfid_focus_index + 1) % 5;
                } else if (key == '4') { // LEFT
                    if (rfid_focus_index == 3) rfid_focus_index = 2;
                    else if (rfid_focus_index == 2) rfid_focus_index = 4;
                    else rfid_focus_index = 0;
                } else if (key == '6') { // RIGHT
                    if (rfid_focus_index == 4) rfid_focus_index = 2;
                    else if (rfid_focus_index == 2) rfid_focus_index = 3;
                    else rfid_focus_index = 1;
                } else if (key == '#') { // PLAY
                    if (rfid_focus_index == 0) {
                        snprintf(rfid_status_msg, sizeof(rfid_status_msg), "TAG READ: 0008432190 OK");
                        Buzzer_Tone(2400, 40); // Scan beep
                    } else if (rfid_focus_index == 1) {
                        snprintf(rfid_status_msg, sizeof(rfid_status_msg), "T5577 WRITE SUCCESS");
                        Buzzer_Tone(1800, 60); // Write beep
                    } else if (rfid_focus_index == 4) {
                        ent_sub_view = 0;
                    }
                }
            } else if (key == '#') {
                ent_sub_view = 0;
            }
        }
        Entertainment_App_Render();
        return;
    }

    if ((current_tick - ent_timer) >= 50) {
        ent_timer = current_tick;
        Entertainment_App_Render();
    }
}
