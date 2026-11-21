#include "oled.h"
#include "font5x7.h"
typedef uint32_t u32; typedef uint8_t u8;
#define REG(a) (*(volatile u32 *)(a))
#define GPIOB_CRL  REG(0x40010C00)
#define GPIOB_IDR  REG(0x40010C08)
#define GPIOB_BSRR REG(0x40010C10)
#define SCL 0
#define SDA 1
#define ADDR 0x3C

static u8 present;
static char text[8][22];
static u8 dirty;                                /* bit per page */

static void dly(void) { for (volatile int i = 0; i < 30; i++) {} }
static void scl(int v) { GPIOB_BSRR = v ? (1u << SCL) : (1u << (SCL + 16)); dly(); }
static void sda(int v) { GPIOB_BSRR = v ? (1u << SDA) : (1u << (SDA + 16)); }
static void i2c_start(void) { sda(1); scl(1); sda(0); dly(); scl(0); }
static void i2c_stop(void)  { sda(0); scl(1); sda(1); dly(); }
static int i2c_byte(u8 b)                       /* returns 1 if the slave acknowledged */
{
    for (int i = 7; i >= 0; i--) { sda((b >> i) & 1); dly(); scl(1); scl(0); }
    sda(1); dly(); scl(1);
    int ack = !(GPIOB_IDR & (1u << SDA));
    scl(0);
    return ack;
}
static int cmd(u8 c) { i2c_start(); int a = i2c_byte(ADDR << 1); i2c_byte(0x00); i2c_byte(c); i2c_stop(); return a; }
static void wait_ms(u32 ms);

int oled_init(void)
{
    GPIOB_CRL = (GPIOB_CRL & ~0xFFu) | 0x66u;   /* PB0, PB1: open-drain outputs, 2 MHz (external pull-ups on the module) */
    GPIOB_BSRR = (1u << SCL) | (1u << SDA);
    wait_ms(50);
    i2c_start(); present = (u8)i2c_byte(ADDR << 1); i2c_stop();
    if (!present) return 0;
    static const u8 init[] = { 0xAE, 0xAD, 0x8A, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0xA1, 0xC8, 0xDA, 0x12,
                               0x81, 0xCF, 0xD9, 0x22, 0xDB, 0x40, 0xA4, 0xA6, 0xAD, 0x8B, 0x33 };
    for (unsigned i = 0; i < sizeof init; i++) cmd(init[i]);
    wait_ms(25);
    cmd(0xAF);
    for (int r = 0; r < 8; r++) oled_line((u8)r, "");
    dirty = 0xFF;
    return 1;
}

void oled_line(u8 row, const char *t)
{
    if (!present || row > 7) return;
    int same = 1, i = 0;
    for (; i < 21 && t[i]; i++) if (text[row][i] != t[i]) same = 0;
    for (int k = i; k < 21; k++) if (text[row][k] != 0) same = 0;   /* shorter text than before: also a change */
    if (same) return;
    for (i = 0; i < 21 && t[i]; i++) text[row][i] = t[i];
    for (; i < 22; i++) text[row][i] = 0;
    dirty |= (u8)(1u << row);
}

void oled_step(void)
{
    if (!present || !dirty) return;
    int p = 0; while (!(dirty & (1u << p))) p++;
    dirty &= (u8)~(1u << p);
    cmd((u8)(0xB0 + p)); cmd(0x02); cmd(0x10);              /* page, column 2 (the SH1106 RAM is 132 wide) */
    i2c_start(); i2c_byte(ADDR << 1); i2c_byte(0x40);
    for (int c = 0; c < 21; c++) {
        char ch = text[p][c];
        for (int x = 0; x < 6; x++) i2c_byte((ch >= 32 && ch <= 126 && x < 5) ? font5x7[ch - 32][x] : 0);
    }
    i2c_byte(0); i2c_byte(0);                               /* 21*6 = 126 columns, pad to 128 */
    i2c_stop();
}

/* SysTick based millisecond wait, only used during start-up (internal 8 MHz clock) */
#define SYST_CSR REG(0xE000E010)
static void wait_ms(u32 ms)
{
    REG(0xE000E014) = 8000u - 1u; REG(0xE000E018) = 0; SYST_CSR = 5u;
    while (ms) { if (SYST_CSR & (1u << 16)) ms--; }
    SYST_CSR = 0;
}

void oled_flush(void) { while (dirty) oled_step(); }
