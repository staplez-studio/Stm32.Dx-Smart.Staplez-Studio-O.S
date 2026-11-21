/* PJ26 hardware diagnostic: no USB, no crystal, no libraries. Runs on the internal 8 MHz clock.
 * 1. Proves the chip runs code: the blue LED (PB11) and PC13 blink.
 * 2. Tests a 128x64 I2C OLED on TWO pin pairs and reports the result as blink codes on the LED:
 *      pair A = PB0 (SCL) / PB1 (SDA)      pair B = PB8 (SCL) / PB9 (SDA)   (the main OS uses pair B)
 *    If the display answers, it is initialised and filled completely white, then shows text.
 * Cycle: 3 fast blinks (I am running) | pause | group A | pause | group B | long pause | repeat.
 * A group is a number of slow blinks:
 *    1 = display answered at address 0x3C     2 = answered at 0x3D
 *    3 = nothing answered (lines are fine)    4 = SCL reads LOW when released (short, or no pull-up)
 *    5 = SDA reads LOW when released (short, or no pull-up)                                          */
#include <stdint.h>
typedef uint32_t u32; typedef uint8_t u8;

#ifndef HOST
#define REG(a) (*(volatile u32 *)(a))
#define RCC_APB2ENR REG(0x40021018)
#define GPIOB_CRL  REG(0x40010C00)
#define GPIOB_CRH  REG(0x40010C04)
#define GPIOB_IDR  REG(0x40010C08)
#define GPIOB_BSRR REG(0x40010C10)
#define GPIOC_CRH  REG(0x40011004)
#define GPIOC_BSRR REG(0x40011010)
#define SYST_CSR REG(0xE000E010)
#define SYST_RVR REG(0xE000E014)
#define SYST_CVR REG(0xE000E018)

static void pin_od(int pin)                         /* PBn open-drain output, released (high) */
{
    volatile u32 *cr = pin < 8 ? &GPIOB_CRL : &GPIOB_CRH; int sh = (pin & 7) * 4;
    *cr = (*cr & ~(0xFu << sh)) | (0x6u << sh);
    GPIOB_BSRR = 1u << pin;
}
static void pin_w(int pin, int v) { GPIOB_BSRR = v ? (1u << pin) : (1u << (pin + 16)); }
static int  pin_r(int pin) { return (GPIOB_IDR >> pin) & 1; }
static void leds(int on)
{
    GPIOB_BSRR = on ? (1u << 11) : (1u << (11 + 16));       /* PJ26 blue LED: active high */
    GPIOC_BSRR = on ? (1u << (13 + 16)) : (1u << 13);       /* classic Blue Pill LED on PC13: active low */
}
static void hw_init(void)
{
    RCC_APB2ENR |= (1u << 3) | (1u << 4);                   /* GPIOB, GPIOC */
    GPIOB_CRH = (GPIOB_CRH & ~(0xFu << 12)) | (0x2u << 12); /* PB11 push-pull 2 MHz */
    GPIOC_CRH = (GPIOC_CRH & ~(0xFu << 20)) | (0x2u << 20); /* PC13 push-pull 2 MHz */
    SYST_RVR = 8000u - 1u; SYST_CVR = 0; SYST_CSR = 5u;     /* 1 ms tick from the 8 MHz clock */
}
static void delay_ms(u32 ms) { while (ms) { if (SYST_CSR & (1u << 16)) ms--; } }
static void dly(void) { for (volatile int i = 0; i < 6; i++) {} }
#else
void pin_od(int pin); void pin_w(int pin, int v); int pin_r(int pin); void leds(int on); void hw_init(void);
void delay_ms(u32 ms); void dly(void);
#endif

#include "font5x7.h"
static int SCL, SDA;
static void scl(int v) { pin_w(SCL, v); dly(); }
static void sda(int v) { pin_w(SDA, v); }
static void start(void) { sda(1); scl(1); sda(0); dly(); scl(0); }
static void stop(void)  { sda(0); scl(1); sda(1); dly(); }
static int  wbyte(u8 b)
{
    for (int i = 7; i >= 0; i--) { sda((b >> i) & 1); dly(); scl(1); scl(0); }
    sda(1); dly(); scl(1);
    int ack = !pin_r(SDA);
    scl(0);
    return ack;
}
static int probe(u8 a7) { start(); int a = wbyte((u8)(a7 << 1)); stop(); return a; }
static int cmd(u8 a7, u8 c) { start(); int a = wbyte((u8)(a7 << 1)); wbyte(0x00); wbyte(c); stop(); return a; }

static void oled_page(u8 a7, int page, const char *txt, int fill)
{
    cmd(a7, (u8)(0xB0 + page)); cmd(a7, 0x02); cmd(a7, 0x10);
    start(); wbyte((u8)(a7 << 1)); wbyte(0x40);
    for (int c = 0; c < 21; c++) {
        char ch = txt ? txt[c] : 0; if (txt && !ch) txt = 0;
        for (int x = 0; x < 6; x++) wbyte(fill ? 0xFF : ((ch >= 32 && ch <= 126 && x < 5) ? font5x7[ch - 32][x] : 0));
    }
    wbyte(fill ? 0xFF : 0); wbyte(fill ? 0xFF : 0);
    stop();
}
static void oled_start(u8 a7)
{
    static const u8 init[] = { 0xAE, 0xAD, 0x8A, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0xA1, 0xC8, 0xDA, 0x12,
                               0x81, 0xCF, 0xD9, 0x22, 0xDB, 0x40, 0xA4, 0xA6, 0xAD, 0x8B, 0x33 };
    for (unsigned i = 0; i < sizeof init; i++) cmd(a7, init[i]);
    delay_ms(25);
    cmd(a7, 0xAF);
}
static const char hex[] = "0123456789ABCDEF";
static void put_pin(char *s, int pin) { if (pin >= 10) { s[0] = '1'; s[1] = (char)('0' + pin - 10); } else { s[0] = (char)('0' + pin); s[1] = ' '; } }

/* returns the blink code; draws on the display when it answers */
int test_pair(int scl_pin, int sda_pin)
{
    SCL = scl_pin; SDA = sda_pin;
    pin_od(SCL); pin_od(SDA); delay_ms(5);
    if (!pin_r(SCL)) return 4;
    if (!pin_r(SDA)) return 5;
    u8 a7 = 0; int code = 3;
    if (probe(0x3C)) { a7 = 0x3C; code = 1; } else if (probe(0x3D)) { a7 = 0x3D; code = 2; }
    if (a7) {
        oled_start(a7);
        for (int p = 0; p < 8; p++) oled_page(a7, p, 0, 1);          /* every pixel on */
        delay_ms(1500);
        char l1[22] = "SCL PB__ SDA PB__", l2[22] = "ADDRESS 0x__";
        put_pin(l1 + 6, scl_pin); put_pin(l1 + 15, sda_pin);
        l2[10] = hex[a7 >> 4]; l2[11] = hex[a7 & 15];
        for (int p = 0; p < 8; p++) oled_page(a7, p, p == 0 ? "PJ26 DIAG: SCREEN OK" : p == 2 ? l1 : p == 3 ? l2 : "", 0);
    }
    return code;
}

static void blink(int n, int on_ms, int off_ms) { for (int i = 0; i < n; i++) { leds(1); delay_ms((u32)on_ms); leds(0); delay_ms((u32)off_ms); } }

void main_c(void)
{
    hw_init();
    for (;;) {
        blink(3, 100, 100);  delay_ms(1500);
        blink(test_pair(0, 1), 350, 350);  delay_ms(1500);       /* group A: PB0 / PB1 */
        blink(test_pair(8, 9), 350, 350);  delay_ms(3000);       /* group B: PB8 / PB9 */
    }
}
