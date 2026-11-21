#include "usb_proto.h"

static const uint8_t dev_desc[18] = {
    18, 1, 0x00, 0x02, 0xEF, 0x02, 0x01, 64, 0x09, 0x12, 0x01, 0x00, 0x00, 0x01, 1, 2, 3, 1 };   /* VID 1209 PID 0001 (pid.codes test) */

static const uint8_t cfg_desc[75] = {
    9, 2, 75, 0, 2, 1, 0, 0x80, 50,
    8, 0x0B, 0, 2, 2, 2, 0, 0,                                  /* interface association: CDC = ifaces 0..1 */
    9, 4, 0, 0, 1, 2, 2, 0, 0,                                  /* communication interface, ACM */
    5, 0x24, 0x00, 0x10, 0x01,                                  /* header */
    5, 0x24, 0x01, 0x00, 0x01,                                  /* call management */
    4, 0x24, 0x02, 0x02,                                        /* ACM: line coding + control line state */
    5, 0x24, 0x06, 0x00, 0x01,                                  /* union */
    7, 5, 0x81, 0x03, 8, 0, 255,                                /* EP1 IN interrupt (notifications, unused) */
    9, 4, 1, 0, 2, 0x0A, 0, 0, 0,                               /* data interface */
    7, 5, 0x02, 0x02, 64, 0, 0,                                 /* EP2 OUT bulk */
    7, 5, 0x83, 0x02, 64, 0, 0 };                               /* EP3 IN bulk */

static const char *const strs[3] = { "PJ26", "PJ26 USB-Serial (ESP flasher)", "0001" };
static uint8_t sbuf[64];
static uint8_t lc[7];

void usbp_init(usbp_t *u)
{
    for (uint8_t *p = (uint8_t *)u; p < (uint8_t *)(u + 1); p++) *p = 0;
    u->baud = 115200; u->bits = 8;
}

void usbp_esp_pins(uint8_t dtr, uint8_t rts, uint8_t *io0_low, uint8_t *en_low)
{
    *en_low  = (uint8_t)(rts && !dtr);        /* RTS only: reset the chip */
    *io0_low = (uint8_t)(dtr && !rts);        /* DTR only: GPIO0 low (boot loader) */
}

static int out(const uint8_t *p, int n, int wlen, const uint8_t **data)
{
    *data = p;
    return n < wlen ? n : wlen;
}

int usbp_setup(usbp_t *u, const uint8_t s[8], const uint8_t **data)
{
    uint8_t type = s[0], req = s[1];
    uint16_t wval = (uint16_t)(s[2] | (s[3] << 8)), wlen = (uint16_t)(s[6] | (s[7] << 8));
    static const uint8_t zero2[2] = {0, 0};
    static uint8_t one;
    *data = 0;

    if ((type & 0x60) == 0x00) {                              /* standard */
        switch (req) {
        case 6: {                                             /* GET_DESCRIPTOR */
            uint8_t t = (uint8_t)(wval >> 8), i = (uint8_t)wval;
            if (!(type & 0x80)) return -1;
            if (t == 1) return out(dev_desc, 18, wlen, data);
            if (t == 2) return out(cfg_desc, 75, wlen, data);
            if (t == 3) {
                if (i == 0) { sbuf[0] = 4; sbuf[1] = 3; sbuf[2] = 9; sbuf[3] = 4; return out(sbuf, 4, wlen, data); }
                if (i > 3) return -1;
                const char *p = strs[i - 1]; int n = 2;
                while (*p && n < 62) { sbuf[n++] = (uint8_t)*p++; sbuf[n++] = 0; }
                sbuf[0] = (uint8_t)n; sbuf[1] = 3;
                return out(sbuf, n, wlen, data);
            }
            return -1;                                        /* device qualifier etc.: full-speed only */
        }
        case 5: u->new_addr = (uint8_t)(wval & 0x7F); u->addr_pending = 1; return 0;        /* SET_ADDRESS */
        case 9: u->configured = (uint8_t)(wval != 0); u->cfg_changed = 1; return 0;          /* SET_CONFIGURATION */
        case 8: one = u->configured; return out(&one, 1, wlen, data);                       /* GET_CONFIGURATION */
        case 0: return out(zero2, 2, wlen, data);                                            /* GET_STATUS */
        case 1: case 3: return 0;                                                            /* CLEAR/SET_FEATURE */
        case 10: one = 0; return out(&one, 1, wlen, data);                                   /* GET_INTERFACE */
        case 11: return 0;                                                                   /* SET_INTERFACE */
        default: return -1;
        }
    }
    if ((type & 0x60) == 0x20) {                              /* class: CDC ACM */
        switch (req) {
        case 0x20:                                            /* SET_LINE_CODING: 7 bytes follow */
            if (wlen != 7) return -1;
            u->out_expect = 1; return 0;
        case 0x21:                                            /* GET_LINE_CODING */
            lc[0] = (uint8_t)u->baud; lc[1] = (uint8_t)(u->baud >> 8); lc[2] = (uint8_t)(u->baud >> 16); lc[3] = (uint8_t)(u->baud >> 24);
            lc[4] = u->stop; lc[5] = u->parity; lc[6] = u->bits;
            return out(lc, 7, wlen, data);
        case 0x22:                                            /* SET_CONTROL_LINE_STATE: bit0 DTR, bit1 RTS */
            u->dtr = (uint8_t)(wval & 1); u->rts = (uint8_t)((wval >> 1) & 1); u->line_changed = 1; return 0;
        case 0x23: return 0;                                  /* SEND_BREAK: ignored */
        default: return -1;
        }
    }
    return -1;
}

void usbp_out_data(usbp_t *u, const uint8_t *d, int len)
{
    if (!u->out_expect || len < 7) return;
    u->out_expect = 0;
    u->baud = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    u->stop = d[4]; u->parity = d[5]; u->bits = d[6];
    u->line_changed = 1;
}
