#include "wifi_net.h"
#include "wifi_config.h"
#include <string.h>

/* ---- calendar maths (Howard Hinnant's civil-date algorithms) ---- */
static int32_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153U * (m + (m > 2 ? -3 : 9)) + 2U) / 5U + d - 1U;
    unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

static void civil_from_days(int32_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    int yy = (int)yoe + era * 400;
    unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    unsigned mp = (5U * doy + 2U) / 153U;
    *d = doy - (153U * mp + 2U) / 5U + 1U;
    *m = mp + (mp < 10 ? 3U : -9U);
    *y = yy + (*m <= 2);
}

int64_t wnet_to_epoch(const wnet_time_t *t)
{
    return (int64_t)days_from_civil(t->year, t->month, t->day) * 86400
         + (int64_t)t->hour * 3600 + (int64_t)t->minute * 60 + t->second;
}

void wnet_from_epoch(int64_t e, wnet_time_t *t)
{
    int64_t days = e / 86400;
    int32_t rem = (int32_t)(e % 86400);
    if (rem < 0) { rem += 86400; days--; }
    int y; unsigned m, d;
    civil_from_days((int32_t)days, &y, &m, &d);
    t->year = (uint16_t)y; t->month = (uint8_t)m; t->day = (uint8_t)d;
    t->hour = (uint8_t)(rem / 3600); t->minute = (uint8_t)((rem % 3600) / 60); t->second = (uint8_t)(rem % 60);
    int32_t wd = (int32_t)((days + 4) % 7);      /* 1970-01-01 was a Thursday */
    if (wd < 0) wd += 7;
    t->wday = (uint8_t)wd;
}

/* ---- "+CIPSNTPTIME:Tue Nov 10 02:15:42 2026" ---- */
static bool parse_uint(const char **p, int *out)
{
    while (**p == ' ') (*p)++;
    if (**p < '0' || **p > '9') return false;
    int v = 0;
    while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; }
    *out = v;
    return true;
}

bool wnet_parse_sntp(const char *line, wnet_time_t *utc)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *p = strstr(line, "+CIPSNTPTIME:");
    if (!p) return false;
    p += 13;
    while (*p == ' ') p++;
    if (!p[0] || !p[1] || !p[2]) return false;
    p += 3;                                   /* weekday name, not needed */
    while (*p == ' ') p++;
    int mon = 0;
    for (int i = 0; i < 12; i++) {
        if (strncmp(p, &months[i * 3], 3) == 0) { mon = i + 1; break; }
    }
    if (!mon) return false;
    p += 3;
    int day, h, mi, s, year;
    if (!parse_uint(&p, &day)) return false;
    if (!parse_uint(&p, &h) || *p != ':') return false;
    p++;
    if (!parse_uint(&p, &mi) || *p != ':') return false;
    p++;
    if (!parse_uint(&p, &s)) return false;
    if (!parse_uint(&p, &year)) return false;
    if (year < 2021 || year > 2099 || day < 1 || day > 31 || h > 23 || mi > 59 || s > 60) return false;  /* 1970 = module has no time yet */
    utc->year = (uint16_t)year; utc->month = (uint8_t)mon; utc->day = (uint8_t)day;
    utc->hour = (uint8_t)h; utc->minute = (uint8_t)mi; utc->second = (uint8_t)(s > 59 ? 59 : s);
    utc->wday = 0;
    wnet_time_t chk; wnet_from_epoch(wnet_to_epoch(utc), &chk);
    utc->wday = chk.wday;
    return true;
}

/* ---- HTTP helpers ---- */
bool wnet_parse_http_date(const char *response, wnet_time_t *utc)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    if (!response) return false;
    const char *end = strstr(response, "\r\n\r\n");
    const char *p = response;
    for (;;) {                                   /* find the header line, case-insensitive */
        if ((p[0] == 'D' || p[0] == 'd') && (p[1] == 'a' || p[1] == 'A') && (p[2] == 't' || p[2] == 'T') &&
            (p[3] == 'e' || p[3] == 'E') && p[4] == ':') break;
        p = strstr(p, "\n");
        if (!p || (end && p > end)) return false;
        p++;
    }
    p += 5;
    while (*p == ' ') p++;
    while (*p && *p != ' ') p++;                 /* "Tue," */
    int day, year, h, mi, s, mon = 0;
    if (!parse_uint(&p, &day)) return false;
    while (*p == ' ') p++;
    for (int i = 0; i < 12; i++) if (strncmp(p, &months[i * 3], 3) == 0) { mon = i + 1; break; }
    if (!mon) return false;
    p += 3;
    if (!parse_uint(&p, &year)) return false;
    if (!parse_uint(&p, &h) || *p != ':') return false;
    p++;
    if (!parse_uint(&p, &mi) || *p != ':') return false;
    p++;
    if (!parse_uint(&p, &s)) return false;
    if (year < 2021 || year > 2099 || day < 1 || day > 31 || h > 23 || mi > 59 || s > 60) return false;
    utc->year = (uint16_t)year; utc->month = (uint8_t)mon; utc->day = (uint8_t)day;
    utc->hour = (uint8_t)h; utc->minute = (uint8_t)mi; utc->second = (uint8_t)(s > 59 ? 59 : s);
    utc->wday = 0;
    wnet_time_t chk; wnet_from_epoch(wnet_to_epoch(utc), &chk);
    utc->wday = chk.wday;
    return true;
}

int wnet_http_status(const char *response)
{
    if (!response || strncmp(response, "HTTP/1.", 7) != 0) return 0;
    const char *sp = strchr(response, ' ');
    if (!sp) return 0;
    int v = 0;
    sp++;
    while (*sp >= '0' && *sp <= '9') { v = v * 10 + (*sp - '0'); sp++; }
    return v;
}

const char *wnet_http_body(const char *response)
{
    if (!response) return NULL;
    const char *p = strstr(response, "\r\n\r\n");
    return p ? p + 4 : NULL;
}

/* ---- weather: "+2°C;80%;Light rain;Toronto, Canada" ---- */
static const char *field(const char *s, int idx, char *out, uint16_t cap)
{
    const char *p = s;
    for (int i = 0; i < idx; i++) {
        p = strchr(p, ';');
        if (!p) { out[0] = 0; return NULL; }
        p++;
    }
    uint16_t n = 0;
    while (*p && *p != ';' && *p != '\r' && *p != '\n') {
        unsigned char c = (unsigned char)*p++;
        if (c < 32 || c > 126) continue;          /* drop the degree sign and other non-ASCII bytes */
        if (n + 1 < cap) out[n++] = (char)c;
    }
    out[n] = 0;
    return p;
}

static void upper_trim(char *s)
{
    char *w = s;
    bool lead = true;
    for (char *r = s; *r; r++) {
        char c = *r;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (lead && c == ' ') continue;
        lead = false;
        *w++ = c;
    }
    while (w > s && w[-1] == ' ') w--;
    *w = 0;
}

bool wnet_parse_weather(const char *body, wnet_weather_t *w)
{
    memset(w, 0, sizeof *w);
    if (!body) return false;
    char t[16], h[12];
    field(body, 0, t, sizeof t);
    field(body, 1, h, sizeof h);
    field(body, 2, w->cond, sizeof w->cond);
    field(body, 3, w->loc, sizeof w->loc);

    /* temperature: optional sign then digits; reject anything without a digit (error pages) */
    const char *p = t;
    bool neg = false, digit = false;
    int v = 0;
    while (*p == ' ' || *p == '+') p++;
    if (*p == '-') { neg = true; p++; }
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; digit = true; }
    if (!digit) return false;
    if (*p != 'C' && *p != 'F' && *p != 0 && *p != ' ') return false;   /* "+2C" / "72F" / bare number */
    w->temp = (int16_t)(neg ? -v : v);

    int hv = 0; bool hd = false;
    for (p = h; *p; p++) { if (*p >= '0' && *p <= '9') { hv = hv * 10 + (*p - '0'); hd = true; } else if (hd) break; }
    w->humidity = hd ? (uint8_t)(hv > 100 ? 100 : hv) : 0;

    upper_trim(w->cond);
    upper_trim(w->loc);
    w->valid = true;
    return true;
}

wnet_icon_t wnet_icon_for(const char *c)
{
    if (!c) return WNET_ICON_CLOUD;
    if (strstr(c, "THUNDER") || strstr(c, "STORM")) return WNET_ICON_STORM;
    if (strstr(c, "SNOW") || strstr(c, "SLEET") || strstr(c, "ICE") || strstr(c, "BLIZZARD")) return WNET_ICON_SNOW;
    if (strstr(c, "RAIN") || strstr(c, "DRIZZLE") || strstr(c, "SHOWER")) return WNET_ICON_RAIN;
    if (strstr(c, "FOG") || strstr(c, "MIST") || strstr(c, "HAZE")) return WNET_ICON_FOG;
    if (strstr(c, "SUN") || strstr(c, "CLEAR")) return WNET_ICON_SUN;
    return WNET_ICON_CLOUD;
}

void wnet_escape_at(const char *in, char *out, uint16_t cap)
{
    uint16_t n = 0;
    for (; *in && n + 3 < cap; in++) {
        if (*in == '"' || *in == ',' || *in == '\\') out[n++] = '\\';
        out[n++] = *in;
    }
    out[n] = 0;
}
