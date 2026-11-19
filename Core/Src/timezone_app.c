/* Time zone settings: UTC offset and daylight-saving rule. Saved in EEPROM. */
#include "timezone_app.h"
#include "staplez_studio.h"
#include "clock_zone.h"
#include "sh1106.h"
#include <stdio.h>
#include <string.h>

static tz_config_t edit;
static uint8_t focus = 0;          /* 0 = offset, 1 = DST rule, 2 = SAVE, 3 = BACK */
static uint32_t tz_timer = 0;

void Timezone_App_Enter(void)
{
    edit = tz_config;
    focus = 0;
}

static void draw_button(uint8_t x, uint8_t w, const char *label, bool focused)
{
    sh1106_drawRect(x, 50, w, 13, SH1106_WHITE);
    if (focused) sh1106_drawRect((uint8_t)(x - 1), 49, (uint8_t)(w + 2), 15, SH1106_WHITE);
    sh1106_drawString((uint8_t)(x + 4), 53, label, 1, SH1106_WHITE);
}

static void Timezone_Render(void)
{
    sh1106_clear();
    sh1106_drawString(4, 2, "TIME ZONE", 1, SH1106_WHITE);
    sh1106_drawHLine(0, 12, 128, SH1106_WHITE);

    char z[16], line[28];
    tz_format_offset(edit.std_offset_min, z, sizeof(z));
    snprintf(line, sizeof(line), "OFFSET:  %s", z);
    sh1106_drawString(6, 17, line, 1, SH1106_WHITE);
    if (focus == 0) sh1106_drawRect(3, 15, 122, 11, SH1106_WHITE);

    snprintf(line, sizeof(line), "DST RULE: %s", tz_rule_name(edit.rule));
    sh1106_drawString(6, 29, line, 1, SH1106_WHITE);
    if (focus == 1) sh1106_drawRect(3, 27, 122, 11, SH1106_WHITE);

    /* what these settings mean for the time on the clock right now */
    bool dst = tz_dst_active_std(&edit, wnet_to_epoch(&tz_std_now));
    int16_t off = (int16_t)(edit.std_offset_min + (dst ? 60 : 0));
    tz_format_offset(off, z, sizeof(z));
    snprintf(line, sizeof(line), "NOW: %s %s", dst ? "DST" : "STD", z);
    sh1106_drawString(6, 40, line, 1, SH1106_WHITE);

    draw_button(14, 40, "SAVE", focus == 2);
    draw_button(72, 40, "BACK", focus == 3);
    sh1106_update();
}

void Timezone_App_Process(char key, uint32_t current_tick)
{
    if (key != 0) {
        if (key == '4') {
            focus = (focus == 0) ? 3 : (uint8_t)(focus - 1);
        } else if (key == '6') {
            focus = (uint8_t)((focus + 1) % 4);
        } else if (key == '2' || key == '8') {
            int dir = (key == '2') ? 1 : -1;
            if (focus == 0) {
                int v = edit.std_offset_min + dir * 15;
                if (v > TZ_OFFSET_MAX_LIMIT) v = TZ_OFFSET_MIN_LIMIT;
                if (v < TZ_OFFSET_MIN_LIMIT) v = TZ_OFFSET_MAX_LIMIT;
                edit.std_offset_min = (int16_t)v;
            } else if (focus == 1) {
                edit.rule = (uint8_t)((edit.rule + TZ_DST_RULE_COUNT + dir) % TZ_DST_RULE_COUNT);
            } else {
                focus = 0;
            }
        } else if (key == '#') {
            if (focus == 2) {
                tz_config = edit;
                TZ_Save();
                DS3231_GetTime();               /* redraw the clock with the new rules at once */
                set_system_state(STATE_SET_TIME_APP);
                return;
            } else if (focus == 3) {
                set_system_state(STATE_SET_TIME_APP);
                return;
            }
            focus++;                            /* on a field: OK moves on */
        } else if (key == '*') {
            set_system_state(STATE_SET_TIME_APP);
            return;
        }
        tz_timer = current_tick;
        Timezone_Render();
        return;
    }
    if ((current_tick - tz_timer) >= 100) {
        tz_timer = current_tick;
        Timezone_Render();
    }
}
