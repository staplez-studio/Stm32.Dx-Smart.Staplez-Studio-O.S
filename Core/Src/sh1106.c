/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    sh1106.c
  * @brief   SH1106 128x64 OLED display driver implementation.
  *          Features calibrated 2px column RAM offset (SEG2..SEG129) and complete
  *          132-column clearing to eliminate all left-edge artifacts.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "sh1106.h"
#include "font5x7.h"
#include <string.h>
#include <stdlib.h>

extern I2C_HandleTypeDef hi2c1;

uint8_t sh1106_buffer[SH1106_WIDTH * SH1106_HEIGHT / 8];
uint8_t sh1106_detected_addr = OLED_I2C_ADDR;

/* 2px hardware offset maps the 128 pixel framebuffer to SEG2..SEG129 of the 132-column RAM */
uint8_t sh1106_column_offset = SH1106_DEFAULT_COLUMN_OFFSET;

void sh1106_set_column_offset(uint8_t offset)
{
    sh1106_column_offset = offset;
}

static void sh1106_cmd(uint8_t c)
{
    HAL_I2C_Mem_Write(&hi2c1, sh1106_detected_addr, 0x00, 1, &c, 1, 50);
}

static void sh1106_cmd2(uint8_t c, uint8_t arg)
{
    uint8_t buf[2] = {c, arg};
    HAL_I2C_Mem_Write(&hi2c1, sh1106_detected_addr, 0x00, 1, buf, 2, 50);
}

HAL_StatusTypeDef sh1106_init(void)
{
    memset(sh1106_buffer, 0, sizeof(sh1106_buffer));

    // Power-on delay for display reset capacitor
    HAL_Delay(50);

    // Auto-detect I2C address (0x3C or 0x3D)
    if (HAL_I2C_IsDeviceReady(&hi2c1, (0x3C << 1), 2, 10) == HAL_OK) {
        sh1106_detected_addr = (0x3C << 1);
    } else if (HAL_I2C_IsDeviceReady(&hi2c1, (0x3D << 1), 2, 10) == HAL_OK) {
        sh1106_detected_addr = (0x3D << 1);
    } else {
        sh1106_detected_addr = (0x3C << 1);
    }

    // 1. Immediately turn OFF display and DC-DC converters
    sh1106_cmd(0xAE);        // Display OFF (Sleep mode)
    sh1106_cmd2(0x8D, 0x10); // Disable SSD1306 charge pump
    sh1106_cmd2(0xAD, 0x8A); // Disable SH1106 DC-DC converter

    // 2. Set basic display registers while display is OFF
    sh1106_cmd2(0xD5, 0x80); // Clock divide ratio / oscillator frequency
    sh1106_cmd2(0xA8, 0x3F); // Multiplex ratio (64 rows for 128x64)
    sh1106_cmd2(0xD3, 0x00); // Display offset = 0
    sh1106_cmd(0x40);        // Display start line = 0
    sh1106_cmd(0xA1);        // Segment re-map: column 131 is SEG0
    sh1106_cmd(0xC8);        // COM output scan direction: remapped
    sh1106_cmd2(0xDA, 0x12); // COM pins configuration: Alternative
    sh1106_cmd2(0x81, 0xFF); // Contrast control: MAX (0xFF)
    sh1106_cmd2(0xD9, 0x22); // Pre-charge period
    sh1106_cmd2(0xDB, 0x40); // VCOMH deselect level
    sh1106_cmd(0xA4);        // Entire display ON: resume to RAM content
    sh1106_cmd(0xA6);        // Normal display (non-inverted)

    // 3. Wipe all 132 columns of RAM on all 8 pages while display is DARK
    uint8_t zero_page[132];
    memset(zero_page, 0, sizeof(zero_page));
    for (uint8_t p = 0; p < SH1106_PAGES; p++) {
        uint8_t clear_cmd[3] = {
            (uint8_t)(0xB0 + p),
            0x00, // lower column 0
            0x10  // higher column 0
        };
        HAL_I2C_Mem_Write(&hi2c1, sh1106_detected_addr, 0x00, 1, clear_cmd, 3, 20);
        HAL_I2C_Mem_Write(&hi2c1, sh1106_detected_addr, 0x40, 1, zero_page, 132, 50);
    }



    // 5. Enable DC-DC charge pump converter for SH1106 and SSD1306 panels
    sh1106_cmd2(0xAD, 0x8B); // SH1106 DC-DC pump enable
    sh1106_cmd(0x33);        // 9.0V pump voltage for bright, crisp display
    sh1106_cmd2(0x8D, 0x14); // SSD1306 charge pump enable

    // Wait 25ms for DC-DC boost voltage to stabilize
    HAL_Delay(25);

    // 6. Turn Display ON!
    sh1106_cmd(0xAF);

    return HAL_OK;
}

HAL_StatusTypeDef sh1106_update(void)
{
    uint8_t col = sh1106_column_offset;

    for (uint8_t p = 0; p < SH1106_PAGES; p++) {
        uint8_t cmd[3] = {
            (uint8_t)(0xB0 + p),
            (uint8_t)(0x00 | (col & 0x0F)),
            (uint8_t)(0x10 | ((col >> 4) & 0x0F))
        };

        HAL_I2C_Mem_Write(&hi2c1, sh1106_detected_addr, 0x00, 1, cmd, 3, 20);
        HAL_I2C_Mem_Write(&hi2c1, sh1106_detected_addr, 0x40, 1, &sh1106_buffer[p * SH1106_WIDTH], SH1106_WIDTH, 50);
    }
    return HAL_OK;
}

void sh1106_clear(void)
{
    memset(sh1106_buffer, 0, sizeof(sh1106_buffer));
}

void sh1106_fill(uint8_t color)
{
    memset(sh1106_buffer, color ? 0xFF : 0x00, sizeof(sh1106_buffer));
}

void sh1106_drawPixel(uint8_t x, uint8_t y, uint8_t color)
{
    if (x >= SH1106_WIDTH || y >= SH1106_HEIGHT) return;
    if (color == SH1106_WHITE) {
        sh1106_buffer[x + (y / 8) * SH1106_WIDTH] |= (1 << (y % 8));
    } else if (color == SH1106_BLACK) {
        sh1106_buffer[x + (y / 8) * SH1106_WIDTH] &= ~(1 << (y % 8));
    } else if (color == SH1106_INVERSE) {
        sh1106_buffer[x + (y / 8) * SH1106_WIDTH] ^= (1 << (y % 8));
    }
}

void sh1106_drawHLine(uint8_t x, uint8_t y, uint8_t w, uint8_t color)
{
    for (uint8_t i = 0; i < w; i++) {
        sh1106_drawPixel(x + i, y, color);
    }
}

void sh1106_drawVLine(uint8_t x, uint8_t y, uint8_t h, uint8_t color)
{
    for (uint8_t i = 0; i < h; i++) {
        sh1106_drawPixel(x, y + i, color);
    }
}

void sh1106_drawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color)
{
    sh1106_drawHLine(x, y, w, color);
    sh1106_drawHLine(x, y + h - 1, w, color);
    sh1106_drawVLine(x, y, h, color);
    sh1106_drawVLine(x + w - 1, y, h, color);
}

void sh1106_fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color)
{
    for (uint8_t i = 0; i < h; i++) {
        sh1106_drawHLine(x, y + i, w, color);
    }
}

void sh1106_drawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t color)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    while (1) {
        sh1106_drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void sh1106_drawCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color)
{
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    sh1106_drawPixel(x0, y0 + r, color);
    sh1106_drawPixel(x0, y0 - r, color);
    sh1106_drawPixel(x0 + r, y0, color);
    sh1106_drawPixel(x0 - r, y0, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        sh1106_drawPixel(x0 + x, y0 + y, color);
        sh1106_drawPixel(x0 - x, y0 + y, color);
        sh1106_drawPixel(x0 + x, y0 - y, color);
        sh1106_drawPixel(x0 - x, y0 - y, color);
        sh1106_drawPixel(x0 + y, y0 + x, color);
        sh1106_drawPixel(x0 - y, y0 + x, color);
        sh1106_drawPixel(x0 + y, y0 - x, color);
        sh1106_drawPixel(x0 - y, y0 - x, color);
    }
}

void sh1106_fillCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color)
{
    sh1106_drawVLine(x0, y0 - r, 2 * r + 1, color);
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        sh1106_drawVLine(x0 + x, y0 - y, 2 * y + 1, color);
        sh1106_drawVLine(x0 - x, y0 - y, 2 * y + 1, color);
        sh1106_drawVLine(x0 + y, y0 - x, 2 * x + 1, color);
        sh1106_drawVLine(x0 - y, y0 - x, 2 * x + 1, color);
    }
}

void sh1106_drawChar(uint8_t x, uint8_t y, char c, uint8_t scale, uint8_t color)
{
    if (c < 32 || c > 126) return;
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t line = font5x7[c - 32][col];
        for (uint8_t row = 0; row < 7; row++) {
            if ((line >> row) & 1) {
                if (scale == 1) {
                    sh1106_drawPixel(x + col, y + row, color);
                } else {
                    sh1106_fillRect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

void sh1106_drawString(uint8_t x, uint8_t y, const char *str, uint8_t scale, uint8_t color)
{
    if (!str) return;
    uint8_t curX = x;
    while (*str) {
        sh1106_drawChar(curX, y, *str, scale, color);
        curX += (6 * scale);
        str++;
    }
}

void sh1106_setContrast(uint8_t contrast)
{
    uint8_t cmd[2] = {0x81, contrast};
    HAL_I2C_Mem_Write(&hi2c1, sh1106_detected_addr, 0x00, 1, cmd, 2, 50);
}

void sh1106_invert(uint8_t invert)
{
    uint8_t cmd = invert ? 0xA7 : 0xA6;
    HAL_I2C_Mem_Write(&hi2c1, sh1106_detected_addr, 0x00, 1, &cmd, 1, 50);
}

void sh1106_displayOn(uint8_t on)
{
    uint8_t cmd = on ? 0xAF : 0xAE;
    HAL_I2C_Mem_Write(&hi2c1, sh1106_detected_addr, 0x00, 1, &cmd, 1, 50);
}
