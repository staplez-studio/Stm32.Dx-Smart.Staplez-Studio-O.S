#include "sd_proto.h"
#include <string.h>

static const char *skip_tag(const char *line, const char *tag)
{
    const char *p = strstr(line, tag);
    return p ? p + strlen(tag) : NULL;
}

static bool read_uint(const char **p, uint32_t *v)
{
    while (**p == ' ') (*p)++;
    if (**p < '0' || **p > '9') return false;
    uint32_t a = 0;
    while (**p >= '0' && **p <= '9') {
        if (a > 429496729UL) a = 429496729UL * 10U;           /* clamp, never wrap */
        else a = a * 10U + (uint32_t)(**p - '0');
        (*p)++;
    }
    *v = a;
    return true;
}

bool sdp_parse_info(const char *line, bool *mounted, uint32_t *total_mb, uint32_t *used_mb)
{
    const char *p = skip_tag(line, "+SDINFO:");
    if (!p) return false;
    uint32_t m, t, u;
    if (!read_uint(&p, &m) || *p != ',') return false;
    p++;
    if (!read_uint(&p, &t) || *p != ',') return false;
    p++;
    if (!read_uint(&p, &u)) return false;
    *mounted = (m != 0);
    *total_mb = t;
    *used_mb = u;
    return true;
}

bool sdp_parse_entry(const char *line, sd_entry_t *e)
{
    const char *p = skip_tag(line, "+SDLS:");
    if (!p) return false;
    while (*p == ' ') p++;
    if (*p != 'd' && *p != 'f') return false;                  /* "END" is not an entry */
    bool dir = (*p == 'd');
    p++;
    if (*p != ',') return false;
    p++;
    uint32_t size;
    if (!read_uint(&p, &size) || *p != ',') return false;
    p++;
    if (*p != '"') return false;
    p++;
    const char *end = strrchr(p, '"');                        /* a file name may contain commas */
    if (!end || end == p) return false;
    uint8_t n = 0;
    for (const char *q = p; q < end && n + 1 < SD_NAME_MAX; q++) {
        unsigned char c = (unsigned char)*q;
        e->name[n++] = (c >= 32 && c <= 126) ? (char)c : '?';  /* the font only has printable ASCII */
    }
    e->name[n] = 0;
    e->size = size;
    e->is_dir = dir;
    return true;
}

bool sdp_parse_end(const char *line, uint16_t *total)
{
    const char *p = skip_tag(line, "+SDLS:END,");
    if (!p) return false;
    uint32_t v;
    if (!read_uint(&p, &v)) return false;
    *total = (uint16_t)(v > 65535U ? 65535U : v);
    return true;
}

bool sdp_is_mp3(const char *name)
{
    size_t n = strlen(name);
    if (n < 5) return false;
    const char *e = name + n - 4;
    return e[0] == '.' && (e[1] == 'm' || e[1] == 'M') && (e[2] == 'p' || e[2] == 'P') && e[3] == '3';
}

static int utoa_(uint32_t v, char *o)
{
    char d[11]; int k = 0, n = 0;
    do { d[k++] = (char)('0' + v % 10U); v /= 10U; } while (v);
    while (k) o[n++] = d[--k];
    return n;
}

void sdp_format_size(uint32_t b, char out[6])
{
    if (b < 1000U) { int n = utoa_(b, out); out[n++] = 'B'; out[n] = 0; return; }
    const char units[] = "KMG";
    uint32_t div = 1000U; uint8_t u = 0;
    while (b >= div * 1000U && u < 2) { div *= 1000U; u++; }
    uint32_t whole = b / div;
    uint32_t tenth = (b % div) * 10U / div;
    if (whole >= 10U) {
        int n = utoa_(whole, out);
        out[n++] = units[u]; out[n] = 0;
    } else {
        out[0] = (char)('0' + whole); out[1] = '.'; out[2] = (char)('0' + tenth); out[3] = units[u]; out[4] = 0;
    }
}

bool sdp_path_join(char *path, uint16_t cap, const char *name)
{
    size_t n = strlen(path), m = strlen(name);
    bool root = (n == 1 && path[0] == '/');
    if (n + (root ? 0U : 1U) + m + 1U > cap) return false;
    if (!root) path[n++] = '/';
    memcpy(path + n, name, m + 1U);
    return true;
}

void sdp_path_parent(char *path)
{
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') n--;
    while (n > 1 && path[n - 1] != '/') n--;
    if (n > 1) n--;                                            /* drop the slash before the name */
    if (n == 0) n = 1;
    path[n] = 0;
    if (path[0] == 0) { path[0] = '/'; path[1] = 0; }
}
