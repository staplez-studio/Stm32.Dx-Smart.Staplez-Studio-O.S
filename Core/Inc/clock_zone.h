#ifndef CLOCK_ZONE_H
#define CLOCK_ZONE_H
/* Time zone, daylight saving and calendar helpers. No hardware access: unit-tested on a PC against
 * the IANA time-zone database.
 *
 * The DS3231 always holds STANDARD time of the chosen zone. Daylight saving is added on top by
 * tz_std_to_local(), so it works with no internet and the clock chip never has to change. */
#include <stdint.h>
#include <stdbool.h>
#include "wifi_net.h"     /* wnet_time_t and the epoch helpers */

typedef enum {
    TZ_DST_NONE = 0,
    TZ_DST_US,        /* US / Canada: 2nd Sun Mar 02:00 -> 1st Sun Nov 02:00 (local clocks)      */
    TZ_DST_EU,        /* Europe / UK: last Sun Mar 01:00 UTC -> last Sun Oct 01:00 UTC             */
    TZ_DST_AU,        /* Australia (SE): 1st Sun Oct 02:00 -> 1st Sun Apr 03:00 (local clocks)    */
    TZ_DST_NZ,        /* New Zealand: last Sun Sep 02:00 -> 1st Sun Apr 03:00 (local clocks)      */
    TZ_DST_RULE_COUNT
} tz_rule_t;

typedef struct {
    int16_t std_offset_min;   /* standard-time offset from UTC in minutes, e.g. -300 for EST */
    uint8_t rule;             /* tz_rule_t */
} tz_config_t;

#define TZ_OFFSET_MIN_LIMIT  (-720)
#define TZ_OFFSET_MAX_LIMIT  (840)
#define TZ_DEFAULT_OFFSET_MIN (-300)      /* Eastern standard time */
#define TZ_DEFAULT_RULE       TZ_DST_US

extern tz_config_t tz_config;             /* the active zone; loaded from EEPROM at boot */
extern bool tz_saved;                     /* true once a zone has been saved (or was found in EEPROM) */

const char *tz_rule_name(uint8_t rule);                                           /* "US/CANADA", ... */
bool    tz_dst_active_std(const tz_config_t *cfg, int64_t std_local_epoch);       /* is DST in effect at this standard time? */
void    tz_std_to_local(const tz_config_t *cfg, const wnet_time_t *std, wnet_time_t *local, bool *dst);
void    tz_local_to_std(const tz_config_t *cfg, const wnet_time_t *local, wnet_time_t *std);
void    tz_utc_to_std(const tz_config_t *cfg, const wnet_time_t *utc, wnet_time_t *std);
int16_t tz_current_offset_min(const tz_config_t *cfg, const wnet_time_t *std);    /* offset in force, incl. DST */
void    tz_format_offset(int16_t offset_min, char *out, uint16_t cap);            /* "UTC-5", "UTC+5:30" */

uint8_t tz_days_in_month(uint16_t year, uint8_t month);
uint8_t tz_weekday(uint16_t year, uint8_t month, uint8_t day);                    /* 0 = Sunday */

#endif
