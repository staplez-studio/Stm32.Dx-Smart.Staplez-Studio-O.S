#ifndef WIFI_NET_H
#define WIFI_NET_H
/* Hardware-independent helpers for Wi-Fi time sync and weather. No HAL calls in here, so they can be
 * unit-tested on a PC. */
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t year;
    uint8_t  month;    /* 1..12 */
    uint8_t  day;      /* 1..31 */
    uint8_t  hour;     /* 0..23 */
    uint8_t  minute;
    uint8_t  second;
    uint8_t  wday;     /* 0 = Sunday */
} wnet_time_t;

typedef struct {
    bool    valid;
    int16_t temp;          /* degrees, in the unit requested */
    uint8_t humidity;      /* percent */
    char    cond[24];      /* e.g. "LIGHT RAIN" */
    char    loc[24];       /* e.g. "TORONTO, CANADA" */
} wnet_weather_t;

typedef enum { WNET_ICON_SUN = 0, WNET_ICON_CLOUD, WNET_ICON_RAIN, WNET_ICON_SNOW, WNET_ICON_STORM, WNET_ICON_FOG } wnet_icon_t;

int64_t wnet_to_epoch(const wnet_time_t *t);            /* seconds since 1970-01-01 (no zone applied) */
void    wnet_from_epoch(int64_t e, wnet_time_t *t);
bool    wnet_parse_sntp(const char *line, wnet_time_t *utc);          /* "+CIPSNTPTIME:Tue Nov 10 02:15:42 2026" */
bool    wnet_parse_weather(const char *body, wnet_weather_t *w);      /* "+2C;80%;Light rain;Toronto" */
bool    wnet_parse_http_date(const char *response, wnet_time_t *utc);   /* "Date: Tue, 10 Nov 2026 02:15:42 GMT" header */
const char *wnet_http_body(const char *response);                      /* pointer to the body, or NULL */
int     wnet_http_status(const char *response);                        /* 200, 404 ... or 0 */
wnet_icon_t wnet_icon_for(const char *condition);
void    wnet_escape_at(const char *in, char *out, uint16_t cap);       /* backslash-escape  "  ,  \  for AT strings */

#endif
