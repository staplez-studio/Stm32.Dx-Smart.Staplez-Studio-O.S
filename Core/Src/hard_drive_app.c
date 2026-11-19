/**
  * @file    hard_drive_app.c
  * @brief   "Hard Drive": browse the FILE NAMES on the ESP32-CAM's micro SD card (nothing is read or changed).
  *          An MP3 can be sent to the ADAU1401 through the ESP32-CAM (PLAY); the DFPlayer only plays the
  *          files on its own card (see Entertainment > MP3 PLAYER).
  */
#include "hard_drive_app.h"
#include "sd_bridge.h"
#include "staplez_studio.h"
#include "sh1106.h"
#include "ui_draw.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define HD_ROWS      3
#define HD_PATH_MAX  64

static char       path[HD_PATH_MAX] = "/";
static char       playing[HD_PATH_MAX] = "";
static sd_entry_t rows[HD_ROWS];
static uint8_t    nrows = 0;
static uint16_t   total = 0;
static uint16_t   top = 0;            /* number of the first visible entry */
static uint8_t    sel = 0;            /* 0..HD_ROWS-1 */
static bool       focus_back = false;
static sd_status_t status = SD_ERROR;
static bool       opened = false;
static bool       dirty = true;
static uint32_t   hd_timer = 0;
static char       hint[20] = "";

void Hard_Drive_App_Init(void)
{
    strcpy(path, "/");
    playing[0] = 0;
    nrows = 0; total = 0; top = 0; sel = 0;
    focus_back = false;
    status = SD_ERROR;
    opened = false;
    dirty = true;
    hint[0] = 0;
}

static void busy_screen(const char *line)
{
    sh1106_clear();
    sh1106_drawString(4, 2, "HARD DRIVE", 1, SH1106_WHITE);
    sh1106_drawHLine(0, 11, 128, SH1106_WHITE);
    sh1106_drawString(4, 28, line, 1, SH1106_WHITE);
    sh1106_update();
}

static void load(void)
{
    busy_screen("READING SD CARD...");
    status = SDBridge_List(path, top, rows, HD_ROWS, &nrows, &total);
    if (status == SD_OK && sel >= nrows && nrows > 0) sel = (uint8_t)(nrows - 1);
    dirty = true;
}

static void open_card(void)
{
    sd_info_t info;
    busy_screen("CONNECTING TO CAM...");
    sd_status_t s = SDBridge_Info(&info);
    opened = true;
    if (s != SD_OK) { status = s; nrows = 0; total = 0; dirty = true; return; }
    top = 0; sel = 0; focus_back = false;
    load();
}

static const sd_entry_t *current(void)
{
    return (status == SD_OK && !focus_back && sel < nrows) ? &rows[sel] : NULL;
}

static uint16_t abs_index(void) { return (uint16_t)(top + sel); }

static void set_hint(const char *s) { snprintf(hint, sizeof hint, "%s", s); }

static void full_path(char *out, uint16_t cap, const char *name)
{
    snprintf(out, cap, "%s", path);
    sdp_path_join(out, cap, name);
}

static void render(void)
{
    sh1106_clear();
    sh1106_drawString(4, 2, "HARD DRIVE", 1, SH1106_WHITE);
    if (status == SD_OK && total > 0) {
        char c[12];
        snprintf(c, sizeof c, "%u/%u", (unsigned)(abs_index() + 1U), (unsigned)total);
        sh1106_drawString((uint8_t)(124 - 6 * (uint8_t)strlen(c)), 2, c, 1, SH1106_WHITE);
    }
    sh1106_drawHLine(0, 11, 128, SH1106_WHITE);

    /* folder line: the end of a long path is what matters */
    char pl[24];
    size_t pn = strlen(path);
    if (pn <= 20) snprintf(pl, sizeof pl, "%s", path);
    else          snprintf(pl, sizeof pl, "~%s", path + pn - 19);
    sh1106_drawString(4, 14, pl, 1, SH1106_WHITE);

    if (status != SD_OK) {
        sh1106_drawString(4, 24, SDBridge_StatusText(status), 1, SH1106_WHITE);
        const char *h = (status == SD_NO_LINK)   ? "CHECK PA2/PA3 WIRES" :
                        (status == SD_NO_BRIDGE) ? "FLASH THE SD BRIDGE" :
                        (status == SD_NO_CARD)   ? "INSERT CARD THEN"    : "PLAY: TRY AGAIN";
        sh1106_drawString(4, 32, h, 1, SH1106_WHITE);
        if (status == SD_NO_CARD || status == SD_NO_BRIDGE || status == SD_NO_LINK)
            sh1106_drawString(4, 40, "PLAY: TRY AGAIN", 1, SH1106_WHITE);
    } else if (total == 0) {
        sh1106_drawString(4, 30, "(EMPTY FOLDER)", 1, SH1106_WHITE);
    } else {
        for (uint8_t i = 0; i < nrows; i++) {
            uint8_t y = (uint8_t)(24 + 9 * i);
            char nm[24];
            if (rows[i].is_dir) snprintf(nm, sizeof nm, "/%.13s", rows[i].name);
            else                snprintf(nm, sizeof nm, "%.14s", rows[i].name);
            sh1106_drawString(6, y, nm, 1, SH1106_WHITE);
            char sz[6];
            if (rows[i].is_dir) snprintf(sz, sizeof sz, "DIR");
            else                sdp_format_size(rows[i].size, sz);
            sh1106_drawString((uint8_t)(122 - 6 * (uint8_t)strlen(sz)), y, sz, 1, SH1106_WHITE);
            if (!focus_back && i == sel) sh1106_drawRect(2, (uint8_t)(y - 1), 124, 9, SH1106_WHITE);
        }
    }

    sh1106_drawRect(2, 51, 30, 12, SH1106_WHITE);
    if (focus_back) sh1106_drawRect(1, 49, 32, 15, SH1106_WHITE);
    sh1106_drawString(5, 54, "BACK", 1, SH1106_WHITE);

    const sd_entry_t *e = current();
    const char *h = hint;
    if (!hint[0] && e) {
        if (e->is_dir) h = "PLAY:OPEN";
        else if (!sdp_is_mp3(e->name)) h = "(NAME ONLY)";
        else { char fp[HD_PATH_MAX]; full_path(fp, sizeof fp, e->name); h = (strcmp(fp, playing) == 0) ? "PLAY:STOP" : "PLAY:PLAY MP3"; }
    }
    sh1106_drawString(38, 54, h, 1, SH1106_WHITE);
    sh1106_update();
    dirty = false;
}

static void handle_key(char key)
{
    hint[0] = 0;
    if (status != SD_OK) {                             /* error screens: retry or leave */
        if (key == '#' && !focus_back) { open_card(); return; }
        if (key == '*' || key == '4' || key == '2' || key == '8' || key == '6') { focus_back = !focus_back; dirty = true; return; }
        if (key == '#' && focus_back) { set_system_state(STATE_ICON_MENU); return; }
        return;
    }

    if (key == '2') {                                  /* UP */
        if (focus_back) { focus_back = false; sel = (nrows ? (uint8_t)(nrows - 1) : 0); }
        else if (sel > 0) sel--;
        else if (top > 0) { top--; load(); }
        dirty = true;
    } else if (key == '8') {                           /* DOWN */
        if (focus_back) { focus_back = false; top = 0; sel = 0; load(); }
        else if (abs_index() + 1U < total) {
            if (sel + 1U < nrows) sel++;
            else { top++; load(); }
        } else focus_back = true;
        dirty = true;
    } else if (key == '4') {                           /* LEFT: parent folder */
        if (strcmp(path, "/") != 0) { sdp_path_parent(path); top = 0; sel = 0; focus_back = false; load(); }
    } else if (key == '*') {                           /* GEAR: jump between the list and BACK */
        focus_back = !focus_back;
        dirty = true;
    } else if (key == '#' || key == '6') {
        if (focus_back) { if (key == '#') set_system_state(STATE_ICON_MENU); return; }
        const sd_entry_t *e = current();
        if (!e) return;
        if (e->is_dir) {
            char np[HD_PATH_MAX];
            snprintf(np, sizeof np, "%s", path);
            if (sdp_path_join(np, sizeof np, e->name)) { strcpy(path, np); top = 0; sel = 0; load(); }
            else set_hint("PATH TOO LONG");
        } else if (key == '#' && sdp_is_mp3(e->name)) {
            char fp[HD_PATH_MAX];
            full_path(fp, sizeof fp, e->name);
            if (strcmp(fp, playing) == 0) {            /* same file again = stop */
                SDBridge_Stop(); playing[0] = 0; set_hint("STOPPED");
            } else {
                sd_status_t s = SDBridge_Play(fp);
                if (s == SD_OK) { snprintf(playing, sizeof playing, "%s", fp); set_hint("PLAYING"); }
                else set_hint(s == SD_BAD_PATH ? "CANT PLAY FILE" : "NO CAM LINK");
            }
        }
        dirty = true;
    }
}

void Hard_Drive_App_Process(char key, uint32_t current_tick)
{
    bool just_opened = (current_tick - hd_timer) > 2000U;       /* called every loop pass while the app is open */
    hd_timer = current_tick;
    if (just_opened || !opened) {
        if (!opened || just_opened) { open_card(); hd_timer = HAL_GetTick(); }
    } else if (key != 0) {
        handle_key(key);
        hd_timer = HAL_GetTick();
        if (current_state != STATE_FILE_EXPLORER_APP) { opened = false; return; }
    }
    if (dirty) render();
}
