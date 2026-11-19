/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    sh1106.h
  * @brief   SH1106 128x64 OLED Display Driver Header for STM32F103 (I2C1).
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __SH1106_H__
#define __SH1106_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define SH1106_WIDTH   128
#define SH1106_HEIGHT  64
#define SH1106_PAGES   8

#define OLED_I2C_ADDR  (0x3C << 1)

/* Hardware column offset:
   0 = 0px offset (standard default, SEG0..SEG127)
   1 = 1px offset
   2 = 2px offset (standard 1.3" SH1106 panels, SEG2..SEG129) */
#define SH1106_DEFAULT_COLUMN_OFFSET 0

#define SH1106_BLACK   0
#define SH1106_WHITE   1
#define SH1106_INVERSE 2

extern uint8_t sh1106_buffer[SH1106_WIDTH * SH1106_HEIGHT / 8];
extern uint8_t sh1106_column_offset;

HAL_StatusTypeDef sh1106_init(void);
HAL_StatusTypeDef sh1106_update(void);
void sh1106_clear(void);
void sh1106_fill(uint8_t color);
void sh1106_drawPixel(uint8_t x, uint8_t y, uint8_t color);
void sh1106_drawHLine(uint8_t x, uint8_t y, uint8_t w, uint8_t color);
void sh1106_drawVLine(uint8_t x, uint8_t y, uint8_t h, uint8_t color);
void sh1106_drawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);
void sh1106_fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);
void sh1106_drawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t color);
void sh1106_drawCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color);
void sh1106_fillCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color);
void sh1106_drawChar(uint8_t x, uint8_t y, char c, uint8_t scale, uint8_t color);
void sh1106_drawString(uint8_t x, uint8_t y, const char *str, uint8_t scale, uint8_t color);
void sh1106_set_column_offset(uint8_t offset);
void sh1106_setContrast(uint8_t contrast);
void sh1106_invert(uint8_t invert);
void sh1106_displayOn(uint8_t on);

#ifdef __cplusplus
}
#endif

#endif /* __SH1106_H__ */
