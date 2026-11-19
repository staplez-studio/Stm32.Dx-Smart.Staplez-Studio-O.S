/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    app_menu.c
  * @brief   7-App Carousel rotary menu matching emulator renderAppMenu to a tee.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "app_menu.h"
#include "staplez_studio.h"
#include "sh1106.h"
#include "ui_draw.h"
#include "weather_icons.h"
#include "wifi_manager.h"
#include "alarm_manager.h"
#include "fan_app.h"
#include "ir_remote_app.h"
#include <stdio.h>
#include <string.h>

#define NUM_APPS 7
#define VISIBLE_APPS 5

static uint8_t selected_app_index = 0; // 0..5 for apps, 6 for BACK button
static uint8_t last_app_index = 0;
static uint8_t window_start = 0;
static uint32_t menu_timer = 0;

void App_Menu_Init(void)
{
    selected_app_index = 0;
    window_start = 0;
    menu_timer = 0;
}

void App_Menu_Render(void)
{
    sh1106_clear();

    // 1. Static header title
    sh1106_drawString(4, 4, "APPS", 1, SH1106_WHITE);
    sh1106_drawHLine(0, 14, 128, SH1106_WHITE);

    const uint16_t *app_icons[NUM_APPS] = {
        sc16_wifi_signal,
        sc16_clock,
        sc16_music_note,
        Fan_contrl,
        soundon_icon,
        game_icon,
        sc16_harddrive
    };

    uint8_t iconY = 20;

    // Draw Left Arrow Icon [<] fixed at the left margin (x=2, y=20)
    draw_16x16_icon(2, iconY, sc16_leftarrow);

    // Draw the 5 visible app icons in the sliding window carousel
    uint8_t startX = 20;
    uint8_t spacing = 18;

    for (uint8_t i = 0; i < VISIBLE_APPS; i++) {
        uint8_t appIdx = window_start + i;
        if (appIdx >= NUM_APPS) break;

        uint8_t curX = startX + (i * spacing);
        draw_16x16_icon(curX, iconY, app_icons[appIdx]);

        // Centered 8x8 status icon underneath applicable 16x16 app icons (y=38)
        uint8_t statusX = curX + 4;
        uint8_t statusY = iconY + 18;

        if (appIdx == 0) {
            draw_8x8_icon(statusX, statusY, wifi_connected ? icon_check_8x8 : icon_cross_8x8);
        } else if (appIdx == 1) {
            draw_8x8_icon(statusX, statusY, icon_check_8x8);
        } else if (appIdx == 2) {
            draw_8x8_icon(statusX, statusY, Any_Alarm_Enabled() ? icon_check_8x8 : icon_cross_8x8);
        } else if (appIdx == 3) {
            draw_8x8_icon(statusX, statusY, Fan_Is_Running() ? icon_check_8x8 : icon_cross_8x8);
        } else if (appIdx == 5) {
            draw_8x8_icon(statusX, statusY, icon_check_8x8);

        }

        // Selection box wrapping ONLY active 16x16 app icon
        if (selected_app_index < NUM_APPS && appIdx == selected_app_index) {
            sh1106_drawRect(curX - 1, iconY - 1, 18, 18, SH1106_WHITE);
        }
    }

    // Draw Right Arrow Icon [>] fixed at the right margin (x=110, y=20)
    draw_16x16_icon(110, iconY, sc16_rightarrow);

    // Permanent BACK button box at the bottom (x=4, y=48, w=28, h=12)
    sh1106_drawRect(4, 48, 28, 12, SH1106_WHITE);
    if (selected_app_index == NUM_APPS) {
        // Highlight outer border of BACK button when selected
        sh1106_drawRect(2, 46, 32, 16, SH1106_WHITE);
    }
    sh1106_drawString(6, 50, "BACK", 1, SH1106_WHITE);

    // Live IR hex display on the bottom right (x=82, y=50)
    const char *last_ir = IR_Get_Last_Hex_Str();
    if (last_ir && strlen(last_ir) > 0) {
        char ir_display[8];
        snprintf(ir_display, sizeof(ir_display), "%.6s", last_ir);
        sh1106_drawString(82, 50, ir_display, 1, SH1106_WHITE);
    }

    sh1106_update();
}

void App_Menu_IR_Process(uint32_t code)
{
    uint16_t c16 = (uint16_t)(code & 0xFFFF);
    uint16_t c16_shift = (uint16_t)((code >> 8) & 0xFFFF);

    // 0xF2F0: UP (Focus app carousel or move to previous app)
    if (code == 0xF2F0 || c16 == 0xF2F0 || c16_shift == 0xF2F0) {
        if (selected_app_index >= NUM_APPS) {
            selected_app_index = window_start;
        } else if (selected_app_index > 0) {
            selected_app_index--;
            if (selected_app_index < window_start) {
                window_start = selected_app_index;
            }
        } else {
            selected_app_index = NUM_APPS - 1;
            window_start = NUM_APPS - VISIBLE_APPS;
        }
        App_Menu_Render();
        return;
    }

    // 0xF2E0: DOWN (Move to next app, STAYS IN APPS - never goes to BACK)
    if (code == 0xF2E0 || c16 == 0xF2E0 || c16_shift == 0xF2E0) {
        if (selected_app_index >= NUM_APPS) {
            selected_app_index = (last_app_index < NUM_APPS) ? last_app_index : 0;
        } else {
            selected_app_index = (selected_app_index + 1) % NUM_APPS;
        }
        if (selected_app_index >= window_start + VISIBLE_APPS) {
            window_start = selected_app_index - VISIBLE_APPS + 1;
        } else if (selected_app_index < window_start) {
            window_start = selected_app_index;
        }
        App_Menu_Render();
        return;
    }

    // 0x72C8: LEFT (Move left in carousel, stays in apps)
    if (code == 0x72C8 || c16 == 0x72C8 || c16_shift == 0x72C8) {
        if (selected_app_index >= NUM_APPS) {
            selected_app_index = (last_app_index < NUM_APPS) ? last_app_index : window_start;
        } else if (selected_app_index > 0) {
            selected_app_index--;
        } else {
            selected_app_index = NUM_APPS - 1;
        }
        if (selected_app_index < window_start) {
            window_start = selected_app_index;
        } else if (selected_app_index >= window_start + VISIBLE_APPS) {
            window_start = selected_app_index - VISIBLE_APPS + 1;
        }
        App_Menu_Render();
        return;
    }

    // 0x72D8: RIGHT (Move right in carousel, stays in apps)
    if (code == 0x72D8 || c16 == 0x72D8 || c16_shift == 0x72D8) {
        if (selected_app_index >= NUM_APPS) {
            selected_app_index = (last_app_index < NUM_APPS) ? last_app_index : window_start;
        } else if (selected_app_index < NUM_APPS - 1) {
            selected_app_index++;
        } else {
            selected_app_index = 0;
        }
        if (selected_app_index >= window_start + VISIBLE_APPS) {
            window_start = selected_app_index - VISIBLE_APPS + 1;
        } else if (selected_app_index < window_start) {
            window_start = selected_app_index;
        }
        App_Menu_Render();
        return;
    }

    // 0x7198: Shift between app carousel and BACK button
    if (code == 0x7198 || c16 == 0x7198 || c16_shift == 0x7198) {
        if (selected_app_index < NUM_APPS) {
            last_app_index = selected_app_index;
            selected_app_index = NUM_APPS;
        } else {
            selected_app_index = (last_app_index < NUM_APPS) ? last_app_index : 0;
            if (selected_app_index < window_start) {
                window_start = selected_app_index;
            } else if (selected_app_index >= window_start + VISIBLE_APPS) {
                window_start = selected_app_index - VISIBLE_APPS + 1;
            }
        }
        App_Menu_Render();
        return;
    }

    // 0x7158: PLAY / ENTER (Progress into selected app or return home if BACK selected)
    if (code == 0x7158 || c16 == 0x7158 || c16_shift == 0x7158) {
        if (selected_app_index == NUM_APPS) {
            set_system_state(STATE_MAIN_SCREEN);
            return;
        }
        switch (selected_app_index) {
            case 0: set_system_state(STATE_WIFI_APP); break;
            case 1: set_system_state(STATE_SET_TIME_APP); break;
            case 2: set_system_state(STATE_ALARM_TIME_APP); break;
            case 3: set_system_state(STATE_FAN_APP); break;
            case 4: set_system_state(STATE_SOUND_APP); break;
            case 5: set_system_state(STATE_ENTERTAINMENT_APP); break;
            case 6: set_system_state(STATE_FILE_EXPLORER_APP); break;
        }
        return;
    }
}

void App_Menu_Process(char key, uint32_t current_tick)
{
    (void)key;
    if ((current_tick - menu_timer) >= 50) {
        menu_timer = current_tick;
        App_Menu_Render();
    }
}
