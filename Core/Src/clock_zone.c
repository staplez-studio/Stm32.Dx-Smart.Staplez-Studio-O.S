#include "clock_zone.h"
#include <stdio.h>
#include <string.h>

tz_config_t tz_config = { TZ_DEFAULT_OFFSET_MIN, TZ_DEFAULT_RULE };
bool tz_saved = false;

/* ---- calendar basics (reuse the epoch maths from wifi_net.c) ---- */
uint8_t tz_weekday(uint16_t y, uint8_t m, uint8_t d)
{
    wnet_time_t t = { y, m, d, 0, 0, 0, 0 }, r;
    wnet_from_epoch(wnet_to_epoch(&t), &r);
    return r.wday;
}

uint8_t tz_days_in_month(uint16_t y, uint8_t m)
{
    static const uint8_t dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12) return 30;
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[m - 1];
}

static int64_t day_epoch(int year, unsigned month, unsigned day, int hour)
{
    wnet_time_t t = { (uint16_t)year, (uint8_t)month, (uint8_t)day, (uint8_t)hour, 0, 0, 0 };
    return wnet_to_epoch(&t);
}

/* Day of month of the n-th Sunday (n >= 1), or of the last Sunday (n = -1) */
static unsigned sunday(int year, unsigned month, int n)
{
    if (n > 0) {
        unsigned wd = tz_weekday((uint16_t)year, (uint8_t)month, 1);
        return 1U + ((7U - wd) % 7U) + 7U * (unsigned)(n - 1);
    }
    unsigned last = tz_days_in_month((uint16_t)year, (uint8_t)month);
    unsigned wd = tz_weekday((uint16_t)year, (uint8_t)month, (uint8_t)last);
    return last - wd;
}

const char *tz_rule_name(uint8_t rule)
{
    switch (rule) {
    case TZ_DST_US: return "US/CANADA";
    case TZ_DST_EU: return "EUROPE/UK";
    case TZ_DST_AU: return "AUSTRALIA";
    case TZ_DST_NZ: return "N.ZEALAND";
    default:        return "OFF";
    }
}

/* All comparisons are made in standard local time ("std_local_epoch" is the calendar time shown by
 * the DS3231 turned into seconds, with no zone applied). */
bool tz_dst_active_std(const tz_config_t *cfg, int64_t e)
{
    if (!cfg || cfg->rule == TZ_DST_NONE) return false;
    wnet_time_t t;
    wnet_from_epoch(e, &t);
    int y = t.year;
    int64_t off = (int64_t)cfg->std_offset_min * 60;

    switch (cfg->rule) {
    case TZ_DST_US: {
        int64_t start = day_epoch(y, 3, sunday(y, 3, 2), 2);        /* 02:00 standard */
        int64_t end   = day_epoch(y, 11, sunday(y, 11, 1), 1);      /* 01:00 standard = 02:00 daylight */
        return e >= start && e < end;
    }
    case TZ_DST_EU: {
        int64_t start = day_epoch(y, 3, sunday(y, 3, -1), 1) + off;  /* 01:00 UTC, expressed in standard local time */
        int64_t end   = day_epoch(y, 10, sunday(y, 10, -1), 1) + off;
        return e >= start && e < end;
    }
    case TZ_DST_AU: {
        int64_t end   = day_epoch(y, 4, sunday(y, 4, 1), 2);         /* 03:00 daylight = 02:00 standard */
        int64_t start = day_epoch(y, 10, sunday(y, 10, 1), 2);       /* 02:00 standard */
        return e < end || e >= start;                                /* southern hemisphere: wraps the new year */
    }
    case TZ_DST_NZ: {
        int64_t end   = day_epoch(y, 4, sunday(y, 4, 1), 2);
        int64_t start = day_epoch(y, 9, sunday(y, 9, -1), 2);
        return e < end || e >= start;
    }
    default:
        return false;
    }
}

void tz_std_to_local(const tz_config_t *cfg, const wnet_time_t *std, wnet_time_t *local, bool *dst)
{
    int64_t e = wnet_to_epoch(std);
    bool d = tz_dst_active_std(cfg, e);
    if (d) e += 3600;
    wnet_from_epoch(e, local);
    if (dst) *dst = d;
}

/* The local time shown on the clock -> the standard time stored in the DS3231.
 * Repeated hour at the end of DST: the first (daylight) occurrence is assumed.
 * Skipped hour at the start of DST: the time is moved forward. */
void tz_local_to_std(const tz_config_t *cfg, const wnet_time_t *local, wnet_time_t *std)
{
    int64_t L = wnet_to_epoch(local);
    int64_t cand = L - 3600;
    wnet_from_epoch(tz_dst_active_std(cfg, cand) ? cand : L, std);
}

void tz_utc_to_std(const tz_config_t *cfg, const wnet_time_t *utc, wnet_time_t *std)
{
    wnet_from_epoch(wnet_to_epoch(utc) + (int64_t)cfg->std_offset_min * 60, std);
}

int16_t tz_current_offset_min(const tz_config_t *cfg, const wnet_time_t *std)
{
    return (int16_t)(cfg->std_offset_min + (tz_dst_active_std(cfg, wnet_to_epoch(std)) ? 60 : 0));
}

void tz_format_offset(int16_t m, char *out, uint16_t cap)
{
    char sign = (m < 0) ? '-' : '+';
    int a = (m < 0) ? -m : m;
    if (a % 60 == 0) snprintf(out, cap, "UTC%c%d", sign, a / 60);
    else             snprintf(out, cap, "UTC%c%d:%02d", sign, a / 60, a % 60);
}
