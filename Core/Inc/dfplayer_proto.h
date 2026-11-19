#ifndef DFPLAYER_PROTO_H
#define DFPLAYER_PROTO_H
/* DFPlayer Mini reply frames: 7E FF 06 <cmd> <feedback> <hi> <lo> <chk hi> <chk lo> EF.
 * Hardware independent, so the parser can be unit-tested on a PC. */
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint8_t buf[10]; uint8_t n; } dfp_parser_t;
typedef struct { uint8_t cmd; uint8_t feedback; uint16_t param; } dfp_frame_t;

void dfp_parser_reset(dfp_parser_t *p);
/* Feed one received byte. Returns true when a complete frame with a valid checksum has just arrived. */
bool dfp_parser_feed(dfp_parser_t *p, uint8_t b, dfp_frame_t *out);

#define DFP_CMD_QUERY_TF_FILES   0x48   /* "how many files does the TF (micro SD) card hold?" */
#define DFP_REPLY_TF_FILES       0x48
#define DFP_REPLY_ERROR          0x40   /* param: 1 busy, 2 sleeping, 3 serial error, 4 checksum, 5 out of range, 6 file not found, 7 inserting, 8 no card... */

#endif
