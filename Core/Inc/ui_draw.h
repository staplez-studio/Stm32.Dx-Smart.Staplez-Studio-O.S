/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    ui_draw.h
  * @brief   Primitive icon drawing utilities on SH1106 OLED display.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __UI_DRAW_H__
#define __UI_DRAW_H__

#include <stdint.h>

void draw_16x16_icon(uint8_t x, uint8_t y, const uint16_t *icon);
void draw_32x16_icon(uint8_t x, uint8_t y, const uint16_t *left, const uint16_t *right);
void draw_long_32x16_icon(uint8_t x, uint8_t y, const uint16_t *left, const uint16_t *right);
void draw_16x32_icon(uint8_t x, uint8_t y, const uint16_t *icon);
void draw_8x8_icon(uint8_t x, uint8_t y, const uint8_t *icon);

#endif /* __UI_DRAW_H__ */
