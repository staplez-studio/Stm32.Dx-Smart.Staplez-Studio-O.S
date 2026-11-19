/* Month calendar for the clock. Works offline: the date comes from the DS3231 with the time zone
 * and daylight-saving rules applied. */
#include "calendar_app.h"
#include "staplez_studio.h"
#include "clock_zone.h"
#include "sh1106.h"
#include <stdio.h>
#include <string.h>

static uint16_t view_year = 2026;
static uint8_t  view_month = 1;
static uint32_t cal_timer = 0;

static const char *const MONTHS[12] = { "JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC" };
static const char *const DAYS[7]    = { "SU","MO","TU","WE","TH","FR","SA" };

void Calendar_App_Enter(void)
{
    view_year = current_year;
    view_month = current_month;
    if (view_month < 1 || view_month > 12) view_month = 1;
}

static void Calendar_Render(void)
{
    sh1106_clear();

    /* title row: month and year on the left, UTC offset (and DST flag) on the right */
    char title[16];
    snprintf(title, sizeof(title), "%s %u", MONTHS[view_month - 1], (unsigned)view_year);
    sh1106_drawString(2, 0, title, 1, SH1106_WHITE);

    /* the zone tag describes the time NOW, so it is only shown on the current month */
    bool is_this_month = (view_year == current_year && view_month == current_month);
    if (is_this_month) {
        char zone[20], tag[24];
        tz_format_offset(tz_offset_now_min, zone, sizeof(zone));
        snprintf(tag, sizeof(tag), "%s%s", zone, tz_dst_now ? " DST" : "");
        sh1106_drawString((uint8_t)(126 - 6 * (uint8_t)strlen(tag)), 0, tag, 1, SH1106_WHITE);
    }

    /* weekday names */
    for (uint8_t c = 0; c < 7; c++) {
        sh1106_drawString((uint8_t)(1 + c * 18 + 3), 8, DAYS[c], 1, SH1106_WHITE);
    }
    sh1106_drawHLine(0, 15, 128, SH1106_WHITE);

    /* day grid: 8 px per row, six rows */
    uint8_t first = tz_weekday(view_year, view_month, 1);
    uint8_t dim = tz_days_in_month(view_year, view_month);

    for (uint8_t d = 1; d <= dim; d++) {
        uint8_t slot = (uint8_t)(first + d - 1);
        uint8_t col = slot % 7, row = slot / 7;
        uint8_t x = (uint8_t)(1 + col * 18);
        uint8_t y = (uint8_t)(16 + row * 8);
        char n[4];
        snprintf(n, sizeof(n), "%u", (unsigned)d);
        uint8_t tx = (uint8_t)(x + (d < 10 ? 9 : 3));
        if (is_this_month && d == current_day_num) {
            sh1106_fillRect((uint8_t)(x + 1), (uint8_t)(y - 1), 16, 9, SH1106_WHITE);
            sh1106_drawString(tx, y, n, 1, SH1106_BLACK);
        } else {
            sh1106_drawString(tx, y, n, 1, SH1106_WHITE);
        }
    }
    sh1106_update();
}

void Calendar_App_Process(char key, uint32_t current_tick)
{
    if (key != 0) {
        if (key == '*') {
            set_system_state(STATE_MAIN_SCREEN);
            return;
        } else if (key == '6') {
            if (view_month >= 12) { view_month = 1; if (view_year < 2099) view_year++; } else view_month++;
        } else if (key == '4') {
            if (view_month <= 1) { view_month = 12; if (view_year > 2000) view_year--; } else view_month--;
        } else if (key == '2') {
            if (view_year < 2099) view_year++;
        } else if (key == '8') {
            if (view_year > 2000) view_year--;
        } else if (key == '#') {
            Calendar_App_Enter();                     /* jump back to today's month */
        }
        cal_timer = current_tick;
        Calendar_Render();
        return;
    }
    if ((current_tick - cal_timer) >= 200) {
        cal_timer = current_tick;
        Calendar_Render();
    }
}
