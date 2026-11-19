/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    ui_draw.c
  * @brief   Primitive icon drawing implementation on SH1106 OLED display.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "ui_draw.h"
#include "sh1106.h"

void draw_16x16_icon(uint8_t x, uint8_t y, const uint16_t *icon)
{
    if (!icon) return;
    for (uint8_t row = 0; row < 16; row++) {
        uint16_t bits = icon[row];
        for (uint8_t col = 0; col < 16; col++) {
            if ((bits >> (15 - col)) & 1) {
                sh1106_drawPixel(x + col, y + row, SH1106_WHITE);
            }
        }
    }
}

void draw_32x16_icon(uint8_t x, uint8_t y, const uint16_t *left, const uint16_t *right)
{
    draw_16x16_icon(x, y, left);
    draw_16x16_icon(x + 16, y, right);
}

void draw_long_32x16_icon(uint8_t x, uint8_t y, const uint16_t *left, const uint16_t *right)
{
    draw_32x16_icon(x, y, left, right);
}

void draw_16x32_icon(uint8_t x, uint8_t y, const uint16_t *icon)
{
    if (!icon) return;
    for (uint8_t row = 0; row < 32; row++) {
        uint16_t bits = icon[row];
        for (uint8_t col = 0; col < 16; col++) {
            if ((bits >> (15 - col)) & 1) {
                sh1106_drawPixel(x + col, y + row, SH1106_WHITE);
            }
        }
    }
}

void draw_8x8_icon(uint8_t x, uint8_t y, const uint8_t *icon)
{
    if (!icon) return;
    for (uint8_t row = 0; row < 8; row++) {
        uint8_t bits = icon[row];
        for (uint8_t col = 0; col < 8; col++) {
            if ((bits >> (7 - col)) & 1) {
                sh1106_drawPixel(x + col, y + row, SH1106_WHITE);
            }
        }
    }
}
