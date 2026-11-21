/* PJ26 native-USB serial adapter for flashing an ESP32-CAM.  Pins used (all from the allowed list):
 *   PA11 = USB D-,  PA12 = USB D+  (needs an external 1.5 k resistor from PA12 to 3V3: the USB "I am here" pull-up)
 *   PA2  = USART2 TX -> cam U0R (GPIO3)       PA3 = USART2 RX <- cam U0T (GPIO1)
 *   PA4  = open-drain -> cam GPIO0 (pulled LOW while the PC asserts DTR only)   [auto boot-loader entry]
 *   PA5  = open-drain -> cam EN/reset pad (pulled LOW while the PC asserts RTS only) [optional wire]
 *   PB0  = OLED SCL, PB1 = OLED SDA (optional 1.3" SH1106 I2C status screen, 3V3 + GND)
 *   PB11 = blue LED (lit while bytes pass)
 * The PC sees a standard USB CDC serial port; baud, DTR and RTS follow what the PC program sets.
 * Clock: 8 MHz crystal x9 = 72 MHz, USB gets 48 MHz (PLL / 1.5). */
#include <stdint.h>
#include "usb_proto.h"
#include "oled.h"
typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
#define REG(a)  (*(volatile u32 *)(a))
#define REG16(a) (*(volatile u16 *)(a))

#define RCC_CR REG(0x40021000)
#define RCC_CFGR REG(0x40021004)
#define RCC_APB2ENR REG(0x40021018)
#define RCC_APB1ENR REG(0x4002101C)
#define FLASH_ACR REG(0x40022000)
#define GPIOA_CRL REG(0x40010800)
#define GPIOA_CRH REG(0x40010804)
#define GPIOA_ODR REG(0x4001080C)
#define GPIOA_BSRR REG(0x40010810)
#define GPIOB_CRH REG(0x40010C04)
#define GPIOB_BSRR REG(0x40010C10)
#define U2_SR REG(0x40004400)
#define U2_DR REG(0x40004404)
#define U2_BRR REG(0x40004408)
#define U2_CR1 REG(0x4000440C)
#define NVIC_ISER1 REG(0xE000E104)

#define USB_BASE 0x40005C00u
#define EPR(n)   REG(USB_BASE + 4u * (n))
#define USB_CNTR REG(USB_BASE + 0x40)
#define USB_ISTR REG(USB_BASE + 0x44)
#define USB_DADDR REG(USB_BASE + 0x4C)
#define USB_BTABLE REG(USB_BASE + 0x50)
#define PMA(off) REG16(0x40006000u + 2u * (off))      /* packet memory: 16-bit values on a 4-byte stride */

#define EP_MASK  0x8F8Fu                              /* bits that are plain read/write or write-0-to-clear */
#define RX_STAT  0x3000u
#define TX_STAT  0x0030u
#define RX_VALID 0x3000u
#define RX_NAK   0x2000u
#define RX_STALL 0x1000u
#define TX_VALID 0x0030u
#define TX_NAK   0x0020u
#define TX_STALL 0x0010u
#define KEEP     (-1)
#define T_BULK 0u
#define T_CTRL 1u
#define T_INT  3u

/* packet buffer layout (PMA byte offsets) */
#define BTABLE_SZ 0x00u
#define EP0_TX 0x040u
#define EP0_RX 0x080u
#define EP1_TX 0x0C0u
#define EP2_RX 0x0D0u
#define EP3_TX 0x110u

static void ep_set(unsigned n, int rx, int tx)
{
    u32 v = EPR(n);
    u32 w = (v & EP_MASK) | 0x8080u;                  /* never clear the CTR flags by accident */
    if (rx != KEEP) w |= (v ^ (u32)rx) & RX_STAT;
    if (tx != KEEP) w |= (v ^ (u32)tx) & TX_STAT;
    EPR(n) = w;
}
static void ep_conf(unsigned n, unsigned type, unsigned ea, int rx, int tx)
{
    u32 v = EPR(n);
    EPR(n) = (type << 9) | ea | 0x8080u | (v & 0x4040u);   /* type/address, and reset both data toggles to 0 */
    ep_set(n, rx, tx);
}
static void clr_ctr_rx(unsigned n) { EPR(n) = (EPR(n) & EP_MASK & ~0x8000u) | 0x0080u; }
static void clr_ctr_tx(unsigned n) { EPR(n) = (EPR(n) & EP_MASK & ~0x0080u) | 0x8000u; }

static void pma_write(u32 off, const u8 *d, int n)
{
    for (int i = 0; i < n; i += 2) PMA(off + (u32)i) = (u16)(d[i] | ((i + 1 < n ? d[i + 1] : 0) << 8));
}
static void pma_read(u32 off, u8 *d, int n)
{
    for (int i = 0; i < n; i += 2) { u16 w = PMA(off + (u32)i); d[i] = (u8)w; if (i + 1 < n) d[i + 1] = (u8)(w >> 8); }
}
static void btable(unsigned n, u32 txaddr, u32 rxaddr, u32 rxcount)
{
    PMA(n * 8u + 0) = (u16)txaddr; PMA(n * 8u + 2) = 0;
    PMA(n * 8u + 4) = (u16)rxaddr; PMA(n * 8u + 6) = (u16)rxcount;
}
#define RXCNT64 0x8400u                                /* 64-byte receive buffer: BL_SIZE=1, NUM_BLOCK=1 */

#define RING 1024u
typedef struct { u8 b[RING]; volatile u32 h, t; } ring_t;
static ring_t to_cam, to_pc;
static usbp_t usb;

void USART2_IRQHandler(void)                           /* cam -> ring (single producer) */
{
    u32 sr = U2_SR;
    if (sr & ((1u << 5) | (1u << 3))) {
        u8 b = (u8)U2_DR;
        if (to_pc.h - to_pc.t < RING) { to_pc.b[to_pc.h & (RING - 1u)] = b; to_pc.h++; }
    }
}

/* ---- EP0 control transfers ---- */
enum { C_IDLE, C_IN, C_STATUS_OUT, C_OUT_DATA, C_STATUS_IN };
static u8 c_state;
static const u8 *c_ptr; static int c_left; static int c_wlen; static u8 c_need_zlp, c_zlp_ok;

static void ep0_send_chunk(void)
{
    int n = c_left > 64 ? 64 : c_left;
    pma_write(EP0_TX, c_ptr, n);
    PMA(2) = (u16)n;                                   /* COUNT_TX of EP0 */
    c_ptr += n; c_left -= n;
    if (n == 64 && c_left == 0 && c_zlp_ok && c_need_zlp == 0) c_need_zlp = 1;   /* exact multiple of 64: end with a short packet */
    else if (c_left == 0) c_need_zlp = 0;
    ep_set(0, KEEP, TX_VALID);
}
static void ep0_zlp(void) { PMA(2) = 0; ep_set(0, KEEP, TX_VALID); }

static void apply_line(void)
{
    u8 io0, en;
    usbp_esp_pins(usb.dtr, usb.rts, &io0, &en);
    GPIOA_BSRR = io0 ? (1u << (4 + 16)) : (1u << 4);   /* open-drain: low = pulled down, high = released */
    GPIOA_BSRR = en  ? (1u << (5 + 16)) : (1u << 5);
    u32 baud = usb.baud < 1200 ? 1200 : (usb.baud > 2000000 ? 2000000 : usb.baud);
    U2_BRR = (36000000u + baud / 2u) / baud;
}

static u8 rx_blocked, tx_busy, tx_last_full, led_flag;
static volatile u32 n_pc_to_cam, n_cam_to_pc;
static u8 seen_host;
static volatile u32 n_reset, n_sof, n_err, n_setup, n_tx0;
static u8 last_setup[8];
#define USB_DEBUG 0          /* 1: lines 3 and 4 show USB packet counters instead of DTR/RTS */
#define USB_FNR REG(USB_BASE + 0x48)
static void usb_enable_data_eps(void)
{
    btable(1, EP1_TX, 0, 0);
    ep_conf(1, T_INT, 1, KEEP, TX_NAK);
    btable(2, 0, EP2_RX, RXCNT64);
    ep_conf(2, T_BULK, 2, RX_VALID, KEEP);
    btable(3, EP3_TX, 0, 0);
    ep_conf(3, T_BULK, 3, KEEP, TX_NAK);
}

static void ep0_rx_done(int setup)
{
    u8 buf[64]; int cnt = (int)(PMA(6) & 0x3FFu);
    if (cnt > 64) cnt = 64;
    pma_read(EP0_RX, buf, cnt);
    if (setup) {
        n_setup++; for (int i = 0; i < 8 && i < cnt; i++) last_setup[i] = buf[i];
        if (cnt < 8) { ep_set(0, RX_STALL, TX_STALL); c_state = C_IDLE; return; }
        const u8 *data = 0;
        c_wlen = buf[6] | (buf[7] << 8);
        usb.out_expect = 0;
        int r = usbp_setup(&usb, buf, &data);
        if (r < 0) { ep_set(0, RX_STALL, TX_STALL); c_state = C_IDLE; return; }
        if (usb.out_expect) { c_state = C_OUT_DATA; ep_set(0, RX_VALID, TX_NAK); return; }      /* SET_LINE_CODING */
        if ((buf[0] & 0x80) && c_wlen > 0) {                                                   /* device -> host */
            c_ptr = data; c_left = r; c_need_zlp = 0; c_zlp_ok = (r < c_wlen); c_state = C_IN;
            ep_set(0, RX_VALID, KEEP);
            ep0_send_chunk();
            return;
        }
        c_state = C_STATUS_IN; ep_set(0, RX_NAK, KEEP); ep0_zlp();
    } else if (c_state == C_OUT_DATA) {
        usbp_out_data(&usb, buf, cnt);
        c_state = C_STATUS_IN; ep_set(0, RX_NAK, KEEP); ep0_zlp();
    } else {                                           /* zero-length status OUT after an IN data stage */
        c_state = C_IDLE; ep_set(0, RX_VALID, TX_NAK);
    }
}

static void ep0_tx_done(void)
{
    n_tx0++;
    if (c_state == C_IN) {
        if (c_left > 0 || c_need_zlp == 1) {
            if (c_left == 0) { c_need_zlp = 2; ep0_zlp(); } else ep0_send_chunk();
        } else { c_state = C_STATUS_OUT; ep_set(0, RX_VALID, TX_NAK); }
    } else if (c_state == C_STATUS_IN) {
        if (usb.addr_pending) { USB_DADDR = 0x80u | usb.new_addr; usb.addr_pending = 0; }
        c_state = C_IDLE; ep_set(0, RX_VALID, TX_NAK);
    }
}

static void usb_reset(void)
{
    seen_host = 1;
    USB_BTABLE = 0;
    btable(0, EP0_TX, EP0_RX, RXCNT64);
    ep_conf(0, T_CTRL, 0, RX_VALID, TX_NAK);
    USB_DADDR = 0x80u;
    c_state = C_IDLE;
    usbp_init(&usb);
    apply_line();
    rx_blocked = 0; tx_busy = 0; tx_last_full = 0;
}

static void usb_poll(void)
{
    u32 is = USB_ISTR;
    if (is & ((1u << 13) | (1u << 14))) { USB_ISTR = ~((1u << 13) | (1u << 14)); n_err++; }   /* bit errors, CRC, PMA overrun */
    if (is & (1u << 9)) { USB_ISTR = ~(1u << 9); n_sof++; }                    /* one per millisecond while a host is talking */
    if (is & (1u << 10)) { USB_ISTR = ~(1u << 10); n_reset++; usb_reset(); return; }
    while ((is = USB_ISTR) & (1u << 15)) {
        unsigned ep = is & 0xFu; u32 v = EPR(ep);
        if (ep == 0) {
            if (v & 0x8000u) { int setup = (v & 0x0800u) != 0; clr_ctr_rx(0); ep0_rx_done(setup); }
            else if (v & 0x0080u) { clr_ctr_tx(0); ep0_tx_done(); }
        } else if (ep == 2 && (v & 0x8000u)) {
            u8 b[64]; int cnt = (int)(PMA(2u * 8u + 6u) & 0x3FFu); if (cnt > 64) cnt = 64;
            pma_read(EP2_RX, b, cnt);
            clr_ctr_rx(2);
            for (int i = 0; i < cnt; i++) { to_cam.b[to_cam.h & (RING - 1u)] = b[i]; to_cam.h++; }
            n_pc_to_cam += (u32)cnt;
            led_flag = 1;
            if (RING - (to_cam.h - to_cam.t) >= 64u) ep_set(2, RX_VALID, KEEP); else rx_blocked = 1;
        } else if (ep == 3 && (v & 0x0080u)) { clr_ctr_tx(3); tx_busy = 0; }
        else if (v & 0x8000u) clr_ctr_rx(ep);
        else if (v & 0x0080u) clr_ctr_tx(ep);
    }
    if (usb.cfg_changed) { usb.cfg_changed = 0; if (usb.configured) { usb_enable_data_eps(); tx_busy = 0; rx_blocked = 0; } }
    if (usb.line_changed) { usb.line_changed = 0; apply_line(); }
    if (rx_blocked && RING - (to_cam.h - to_cam.t) >= 64u) { rx_blocked = 0; ep_set(2, RX_VALID, KEEP); }
    if (usb.configured && !tx_busy) {                  /* cam -> PC */
        u32 avail = to_pc.h - to_pc.t;
        if (avail || tx_last_full) {
            u8 b[64]; int n = avail > 64u ? 64 : (int)avail;
            for (int i = 0; i < n; i++) b[i] = to_pc.b[(to_pc.t + (u32)i) & (RING - 1u)];
            to_pc.t += (u32)n;
            pma_write(EP3_TX, b, n);
            PMA(3u * 8u + 2u) = (u16)n;
            n_cam_to_pc += (u32)n;
            tx_last_full = (n == 64);                  /* a full packet is followed by a short/empty one when the stream pauses */
            tx_busy = 1; led_flag = 1;
            ep_set(3, KEEP, TX_VALID);
        }
    }
}


/* ---- OLED status screen (all text, 8 lines x 21 characters) ---- */
static int put(char *o, int n, const char *s) { while (*s && n < 21) o[n++] = *s++; o[n] = 0; return n; }
static int putu(char *o, int n, u32 v) { char d[11]; int k = 0; do { d[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 10); while (k && n < 21) o[n++] = d[--k]; o[n] = 0; return n; }

static void ui_update(void)
{
    char l[22]; int n; u8 io0, en;
    usbp_esp_pins(usb.dtr, usb.rts, &io0, &en);
    oled_line(0, "PJ26 USB-SERIAL");
    n = put(l, 0, "USB: ");
    put(l, n, !seen_host ? "WAITING FOR PC" : !usb.configured ? "CONNECTING" : (usb.dtr || usb.rts) ? "PORT OPEN" : "READY");
    oled_line(1, l);
    n = put(l, 0, "BAUD: "); putu(l, n, usb.baud); oled_line(2, l);
#if USB_DEBUG
    n = put(l, 0, "SU:"); n = putu(l, n, n_setup); n = put(l, n, " TX:"); n = putu(l, n, n_tx0); n = put(l, n, " ER:"); putu(l, n, n_err); oled_line(3, l);
    { static const char hx[] = "0123456789ABCDEF"; n = put(l, 0, "RQ:");
      for (int i = 0; i < 8 && n < 20; i++) { l[n++] = hx[last_setup[i] >> 4]; l[n++] = hx[last_setup[i] & 15]; } l[n] = 0; }
    oled_line(4, l);
#else
    n = put(l, 0, "DTR:"); n = putu(l, n, usb.dtr); n = put(l, n, " RTS:"); putu(l, n, usb.rts); oled_line(3, l);
    n = put(l, 0, "IO0:"); n = put(l, n, io0 ? "LOW " : "HIGH"); n = put(l, n, " EN:"); put(l, n, en ? "RESET" : "RUN"); oled_line(4, l);
#endif
    n = put(l, 0, "PC>CAM "); putu(l, n, n_pc_to_cam); oled_line(5, l);
    n = put(l, 0, "CAM>PC "); putu(l, n, n_cam_to_pc); oled_line(6, l);
    u32 f = USB_FNR;                                     /* RXDP/RXDM: the two USB data lines as the chip sees them */
    n = put(l, 0, "D+:"); n = putu(l, n, (f >> 15) & 1u); n = put(l, n, " D-:"); n = putu(l, n, (f >> 14) & 1u);
    n = put(l, n, " R:"); n = putu(l, n, n_reset); n = put(l, n, " S:"); putu(l, n, n_sof);
    oled_line(7, l);
}

static void halt_msg(void) { for (;;) oled_flush(); }   /* failure: leave the message on the screen */

void main_c(void)
{
    /* 1. screen first, on the internal 8 MHz clock, so every later start-up step can be reported on it */
    RCC_APB2ENR |= (1u << 2) | (1u << 3);              /* GPIOA, GPIOB */
    int have_oled = oled_init();
    oled_line(0, "PJ26 USB-SERIAL"); oled_line(1, "BOOT OK"); oled_flush();

    /* 2. clock 72 MHz from the crystal (USB needs it). Each step has a time-out and reports a failure */
    u32 t = 600000u;
    RCC_CR |= (1u << 16);
    while (!(RCC_CR & (1u << 17)) && --t) {}
    if (!(RCC_CR & (1u << 17))) { oled_line(1, "FAIL: NO CRYSTAL"); oled_line(2, "8 MHZ XTAL (HSE)"); oled_line(3, "DID NOT START"); halt_msg(); }
    FLASH_ACR = (1u << 4) | 2u;
    RCC_CFGR = (1u << 16) | (7u << 18) | (4u << 8);   /* PLL from HSE x9, APB1 /2; USBPRE=0 -> 72/1.5 = 48 MHz */
    RCC_CR |= (1u << 24);
    t = 600000u;
    while (!(RCC_CR & (1u << 25)) && --t) {}
    if (!(RCC_CR & (1u << 25))) { oled_line(1, "FAIL: PLL NO LOCK"); halt_msg(); }
    RCC_CFGR = (RCC_CFGR & ~3u) | 2u;
    t = 600000u;
    while ((((RCC_CFGR >> 2) & 3u) != 2u) && --t) {}
    if (((RCC_CFGR >> 2) & 3u) != 2u) { oled_line(1, "FAIL: CLOCK SWITCH"); halt_msg(); }
    oled_line(1, "CLOCK 72MHZ OK"); oled_flush();
    (void)have_oled;

    RCC_APB1ENR |= (1u << 17) | (1u << 23);            /* USART2, USB */
    GPIOA_ODR |= (1u << 3) | (1u << 4) | (1u << 5);    /* RX pull-up; open-drain outputs idle released */
    GPIOA_CRL = (GPIOA_CRL & ~(0xFFFFu << 8)) | (0xBu << 8) | (0x8u << 12) | (0x6u << 16) | (0x6u << 20);
                /* PA2 AF push-pull, PA3 input pull-up, PA4/PA5 general open-drain 2 MHz (CNF=01 MODE=10 = 0x6) */
    GPIOB_CRH = (GPIOB_CRH & ~(0xFu << 12)) | (0x2u << 12);   /* PB11 LED */

    U2_BRR = 36000000u / 115200u;
    U2_CR1 = (1u << 13) | (1u << 3) | (1u << 2) | (1u << 5);  /* UE TE RE RXNEIE */
    NVIC_ISER1 = (1u << (38 - 32));

    USB_CNTR = 1u;                                     /* FRES: power up, hold in reset */
    for (volatile int i = 0; i < 2000; i++) {}         /* tSTARTUP */
    USB_CNTR = 0;
    USB_ISTR = 0;
    usb_reset();
    seen_host = 0;
    oled_line(1, "USB: STARTING");

    u32 led = 0, ui_div = 0;
    for (;;) {
        usb_poll();
        if (++ui_div >= 60000u) { ui_div = 0; ui_update(); }   /* about every 100-200 ms */
        oled_step();
        if (to_cam.h != to_cam.t && (U2_SR & (1u << 7))) { U2_DR = to_cam.b[to_cam.t & (RING - 1u)]; to_cam.t++; }
        if (led_flag) { led_flag = 0; led = 400000u; }
        GPIOB_BSRR = led ? (1u << 11) : (1u << (11 + 16));
        if (led) led--;
    }
}
