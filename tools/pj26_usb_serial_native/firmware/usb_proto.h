#ifndef USB_PROTO_H
#define USB_PROTO_H
/* Hardware-independent part of the USB CDC-ACM device: descriptors and control requests. */
#include <stdint.h>
typedef struct {
    uint32_t baud; uint8_t stop, parity, bits;      /* CDC line coding */
    uint8_t dtr, rts;                               /* CDC control lines */
    uint8_t configured;
    uint8_t new_addr, addr_pending;                 /* SET_ADDRESS takes effect after the status stage */
    uint8_t out_expect;                             /* a SET_LINE_CODING data stage is expected (7 bytes) */
    uint8_t cfg_changed, line_changed;              /* flags for the hardware layer */
} usbp_t;

void usbp_init(usbp_t *u);
/* Handle an 8-byte SETUP packet. Returns the number of bytes to send in the IN data stage (already cut to wLength),
 * 0 for a request that only needs the zero-length status reply, or -1 to STALL. *data points at the reply. */
int  usbp_setup(usbp_t *u, const uint8_t s[8], const uint8_t **data);
void usbp_out_data(usbp_t *u, const uint8_t *d, int len);   /* data stage of SET_LINE_CODING */
/* ESP auto-reset truth table (classic DTR/RTS circuit): which ESP pins to pull LOW. */
void usbp_esp_pins(uint8_t dtr, uint8_t rts, uint8_t *io0_low, uint8_t *en_low);
#endif
