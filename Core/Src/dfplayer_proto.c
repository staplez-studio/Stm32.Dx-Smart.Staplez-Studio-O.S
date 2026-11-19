#include "dfplayer_proto.h"

void dfp_parser_reset(dfp_parser_t *p) { p->n = 0; }

bool dfp_parser_feed(dfp_parser_t *p, uint8_t b, dfp_frame_t *out)
{
    if (p->n == 0 && b != 0x7E) return false;            /* wait for the start byte */
    p->buf[p->n++] = b;
    if (p->n < 10) {
        if (p->n == 2 && p->buf[1] != 0xFF) { p->n = 0; if (b == 0x7E) p->buf[p->n++] = b; }
        else if (p->n == 3 && p->buf[2] != 0x06) { p->n = 0; if (b == 0x7E) p->buf[p->n++] = b; }
        return false;
    }
    p->n = 0;
    if (p->buf[9] != 0xEF) return false;
    uint16_t sum = 0;
    for (uint8_t i = 1; i <= 6; i++) sum = (uint16_t)(sum + p->buf[i]);
    uint16_t chk = (uint16_t)((p->buf[7] << 8) | p->buf[8]);
    if ((uint16_t)(sum + chk) != 0) return false;
    out->cmd = p->buf[3];
    out->feedback = p->buf[4];
    out->param = (uint16_t)((p->buf[5] << 8) | p->buf[6]);
    return true;
}
