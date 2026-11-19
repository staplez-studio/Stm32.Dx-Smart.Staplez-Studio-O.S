/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    fan_app.c
  * @brief   Fan Controller UI matching emulator renderScreenLayout to a tee.
  *          Controlled 100% by IR Remote (Left, Right, Up, Down, Play, Shift).
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "fan_app.h"
#include "tim.h"
#include "staplez_studio.h"
#include "sh1106.h"
#include "ui_draw.h"
#include "weather_icons.h"
#include <stdbool.h>
#include <stdio.h>
#include <math.h>

static uint8_t fan_speed_percent = 50;
static bool fan_on = true;
static bool fan_auto = false;
static uint8_t fan_eq_level = 11; // 0..21
static uint8_t fan_selected_item = 0; // 0: icon, 1: auto, 2: back
static float fan_angle_rad = 0.0f;
static uint32_t fan_timer = 0;

void Fan_App_Init(void)
{
    fan_speed_percent = 50;
    fan_on = true;
    fan_auto = false;
    fan_eq_level = 11;
    fan_selected_item = 0;
    fan_angle_rad = 0.0f;
    fan_timer = 0;
    Fan_Set_PWM(50);
}

bool Fan_Is_Running(void)
{
    return fan_on;
}

static void draw_computer_fan(int16_t cx, int16_t cy, float angle, uint8_t is_running)
{
    (void)is_running;
    // 1. Fan Frame outer shroud
    sh1106_drawRect(cx - 21, cy - 21, 42, 42, SH1106_WHITE);
    // Mounting ear cutouts
    sh1106_drawPixel(cx - 19, cy - 19, SH1106_WHITE);
    sh1106_drawPixel(cx + 19, cy - 19, SH1106_WHITE);
    sh1106_drawPixel(cx - 19, cy + 19, SH1106_WHITE);
    sh1106_drawPixel(cx + 19, cy + 19, SH1106_WHITE);

    // 2. Circular Shroud Ring (radius 20)
    sh1106_drawCircle(cx, cy, 20, SH1106_WHITE);

    // 3. Central Motor Hub (radius 5)
    sh1106_fillCircle(cx, cy, 5, SH1106_WHITE);

    // 4. Rotating Blades (7 blades)
    uint8_t num_blades = 7;
    float step = 6.283185f / num_blades;
    for (uint8_t i = 0; i < num_blades; i++) {
        float a = angle + (i * step);
        int16_t x1 = cx + (int16_t)(cosf(a) * 6.0f);
        int16_t y1 = cy + (int16_t)(sinf(a) * 6.0f);
        int16_t x2 = cx + (int16_t)(cosf(a + 0.35f) * 19.0f);
        int16_t y2 = cy + (int16_t)(sinf(a + 0.35f) * 19.0f);
        sh1106_drawLine(x1, y1, x2, y2, SH1106_WHITE);
    }
}

void Fan_App_Render(void)
{
    sh1106_clear();

    // 1. "FAN" top left at (2, 2)
    sh1106_drawString(2, 2, "FAN", 1, SH1106_WHITE);

    // "auto" at (30, 2)
    sh1106_drawString(30, 2, "auto", 1, SH1106_WHITE);

    // 8x8 check / cross icon above EQ slider at (59, 3)
    draw_8x8_icon(59, 3, fan_auto ? icon_check_8x8 : icon_cross_8x8);
    if (fan_selected_item == 1) {
        sh1106_drawRect(57, 1, 12, 12, SH1106_WHITE);
    }

    // 2. Computer Fan animation at (27, 37)
    draw_computer_fan(27, 37, fan_angle_rad, fan_on);

    // 3. 16x32 EQ slider bitmap next to fan at (55, 14)
    uint8_t slider_idx = 21 - fan_eq_level;
    if (slider_idx > 21) slider_idx = 21;
    draw_16x32_icon(55, 14, eq_slider_table[slider_idx]);

    // 4. Fan Control Icon under EQ slider at (55, 46), inside its 19x18 selection box
    draw_16x16_icon(55, 46, Fan_contrl);
    if (fan_selected_item == 0) {
        sh1106_drawRect(54, 45, 19, 18, SH1106_WHITE);
    }

    // Hairline vertical divider at x=72
    for (uint8_t y = 3; y < 48; y += 3) {
        sh1106_drawPixel(72, y, SH1106_WHITE);
    }

    // 5. Right Side Telemetry at x=74
    sh1106_drawString(74, 2, "120mm:cw", 1, SH1106_WHITE);

    char pwmBuf[16];
    snprintf(pwmBuf, sizeof(pwmBuf), "PWM: %d%%", fan_speed_percent);
    sh1106_drawString(74, 12, pwmBuf, 1, SH1106_WHITE);

    char rpmBuf[16];
    uint16_t rpm = fan_on ? (uint16_t)(fan_speed_percent * 24) : 0;
    snprintf(rpmBuf, sizeof(rpmBuf), "%u RPM", rpm);
    sh1106_drawString(74, 22, rpmBuf, 1, SH1106_WHITE);

    char lvlBuf[16];
    snprintf(lvlBuf, sizeof(lvlBuf), "LV %d/21", fan_eq_level);
    sh1106_drawString(74, 32, lvlBuf, 1, SH1106_WHITE);

    const char *stateStr = fan_on ? (fan_speed_percent >= 100 ? "TURBO MAX" : "RUNNING") : "PAUSED";
    sh1106_drawString(74, 41, stateStr, 1, SH1106_WHITE);

    // 6. BACK Button in a square at (83, 51, 36, 11)
    sh1106_drawRect(83, 51, 36, 11, SH1106_WHITE);
    if (fan_selected_item == 2) {
        sh1106_drawRect(81, 49, 40, 15, SH1106_WHITE);
    }
    sh1106_drawString(89, 53, "BACK", 1, SH1106_WHITE);

    sh1106_update();
}

void Fan_App_Process(char key, uint32_t current_tick)
{
    if (key != 0) {
        if (key == '2') {
            if (fan_eq_level < 21) {
                fan_eq_level++;
                fan_speed_percent = (uint8_t)((fan_eq_level * 100) / 21);
                if (fan_on) Fan_Set_PWM(fan_speed_percent);
            }
        } else if (key == '8') {
            if (fan_eq_level > 0) {
                fan_eq_level--;
                fan_speed_percent = (uint8_t)((fan_eq_level * 100) / 21);
                if (fan_on) Fan_Set_PWM(fan_speed_percent);
            }
        } else if (key == '4') {
            fan_selected_item = (fan_selected_item == 0) ? 2 : fan_selected_item - 1;
        } else if (key == '6') {
            fan_selected_item = (fan_selected_item + 1) % 3;
        } else if (key == '*') {
            if (fan_selected_item != 2) {
                fan_selected_item = 2;
            } else {
                fan_selected_item = 0;
            }
        } else if (key == '#' || key == '5' || key == 'C') {
            if (fan_selected_item == 0) {
                fan_on = !fan_on;
                Fan_Set_PWM(fan_on ? fan_speed_percent : 0);
            } else if (fan_selected_item == 1) {
                fan_auto = !fan_auto;
            } else if (fan_selected_item == 2) {
                set_system_state(STATE_ICON_MENU);
                return;
            }
        }
        Fan_App_Render();
        return;
    }

    if (fan_on) {
        fan_angle_rad += (fan_speed_percent * 0.005f);
        if (fan_angle_rad > 6.283185f) fan_angle_rad -= 6.283185f;
    }

    if ((current_tick - fan_timer) >= 50) {
        fan_timer = current_tick;
        Fan_App_Render();
    }
}
