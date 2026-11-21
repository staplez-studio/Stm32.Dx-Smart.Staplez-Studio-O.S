#ifndef OLED_H
#define OLED_H
/* 1.3" SH1106 128x64 I2C status screen on PB0 (SCL) / PB1 (SDA), bit-banged, text only (8 lines of 21 characters).
 * Drawn one page (about 5 ms) at a time so USB keeps being served. If no display answers it stays off. */
#include <stdint.h>
int  oled_init(void);                           /* 1 = display found */
void oled_line(uint8_t row, const char *text);  /* row 0..7; marks the page dirty only when the text changed */
void oled_flush(void);                          /* write every dirty page now (start-up only) */
void oled_step(void);                           /* call every loop: writes at most one dirty page */
#endif
