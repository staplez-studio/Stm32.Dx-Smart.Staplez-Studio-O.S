#include "wifi_store.h"
#include "eeprom_map.h"
#include "i2c.h"
#include <string.h>
#include <stdio.h>

/* One slot = 128 bytes: [0]=magic [1]=ssid_len [2]=pwd_len [3]=use counter(low) [4]=use counter(high)
 * [5..36]=ssid [37..100]=password [101]=checksum. The use counter orders the networks by recency. */
#define SLOT_SIZE   128U
#define SLOT_USED   102U
#define SLOT_MAGIC  0xA8
#define EE_ADDR     (0x57U << 1)

/* The single-network record written by earlier firmware (imported once) */
#define LEGACY_MAGIC 0xA7
#define LEGACY_SIZE  100U

typedef struct { wstore_net_t net; uint16_t use; uint8_t slot; } entry_t;

static entry_t  ent[WSTORE_MAX];      /* kept sorted: most recent first */
static uint8_t  count = 0;

static uint8_t checksum(const uint8_t *b, uint8_t n)
{
    uint8_t s = 0x3C;
    for (uint8_t i = 0; i < n; i++) s = (uint8_t)(s + b[i] * 5U + i);
    return s;
}

static bool ee_write(uint16_t addr, const uint8_t *d, uint16_t len)
{
    for (uint16_t off = 0; off < len; off += 16U) {        /* 16 byte chunks never cross a 32 byte page */
        uint16_t n = (uint16_t)(((len - off) > 16U) ? 16U : (len - off));
        if (HAL_I2C_Mem_Write(&hi2c1, EE_ADDR, (uint16_t)(addr + off), I2C_MEMADD_SIZE_16BIT, (uint8_t *)&d[off], n, 100) != HAL_OK) return false;
        HAL_Delay(12);
    }
    return true;
}

static uint16_t slot_addr(uint8_t slot) { return (uint16_t)(EEPROM_WIFILIST_ADDR + slot * SLOT_SIZE); }

static bool write_slot(const entry_t *e)
{
    uint8_t b[SLOT_USED];
    memset(b, 0, sizeof b);
    b[0] = SLOT_MAGIC;
    b[1] = (uint8_t)strlen(e->net.ssid);
    b[2] = (uint8_t)strlen(e->net.pwd);
    b[3] = (uint8_t)(e->use & 0xFF);
    b[4] = (uint8_t)(e->use >> 8);
    memcpy(&b[5], e->net.ssid, b[1]);
    memcpy(&b[37], e->net.pwd, b[2]);
    b[100] = e->net.hidden ? 1 : 0;
    b[SLOT_USED - 1] = checksum(b, SLOT_USED - 1);
    return ee_write(slot_addr(e->slot), b, SLOT_USED);
}

static void erase_slot(uint8_t slot)
{
    uint8_t z[16] = {0};
    ee_write(slot_addr(slot), z, sizeof z);                  /* wipes the magic byte */
}

static void sort_entries(void)                               /* highest use counter first */
{
    for (uint8_t i = 1; i < count; i++) {
        entry_t t = ent[i]; int j = i - 1;
        while (j >= 0 && ent[j].use < t.use) { ent[j + 1] = ent[j]; j--; }
        ent[j + 1] = t;
    }
}

static uint16_t next_use(void)
{
    uint16_t m = 0;
    for (uint8_t i = 0; i < count; i++) if (ent[i].use > m) m = ent[i].use;
    return (uint16_t)(m + 1U);
}

/* The 16 bit counter would wrap after 65535 joins: renumber 1..n (rewrites the slots; extremely rare). */
static void renumber_if_needed(void)
{
    if (next_use() < 0xFFF0U) return;
    for (uint8_t i = 0; i < count; i++) { ent[i].use = (uint16_t)(count - i); write_slot(&ent[i]); }
}

void WStore_Load(void)
{
    count = 0;
    for (uint8_t s = 0; s < EEPROM_WIFILIST_SLOTS; s++) {
        uint8_t b[SLOT_USED];
        if (HAL_I2C_Mem_Read(&hi2c1, EE_ADDR, slot_addr(s), I2C_MEMADD_SIZE_16BIT, b, SLOT_USED, 100) != HAL_OK) continue;
        if (b[0] != SLOT_MAGIC || b[1] == 0 || b[1] > WSTORE_SSID_MAX || b[2] > WSTORE_PWD_MAX) continue;
        if (b[SLOT_USED - 1] != checksum(b, SLOT_USED - 1)) continue;
        entry_t *e = &ent[count];
        memset(e, 0, sizeof *e);
        memcpy(e->net.ssid, &b[5], b[1]);
        memcpy(e->net.pwd, &b[37], b[2]);
        e->net.hidden = (b[100] == 1);
        e->use = (uint16_t)(b[3] | (b[4] << 8));
        e->slot = s;
        bool dup = false;                                    /* never two entries with one name */
        for (uint8_t i = 0; i < count; i++) if (strcmp(ent[i].net.ssid, e->net.ssid) == 0) dup = true;
        if (!dup) count++;
    }
    sort_entries();

    /* One-time import of the old single saved network */
    if (count == 0) {
        uint8_t b[LEGACY_SIZE];
        if (HAL_I2C_Mem_Read(&hi2c1, EE_ADDR, EEPROM_WIFI_ADDR, I2C_MEMADD_SIZE_16BIT, b, LEGACY_SIZE, 100) == HAL_OK &&
            b[0] == LEGACY_MAGIC && b[1] > 0 && b[1] <= WSTORE_SSID_MAX && b[2] <= WSTORE_PWD_MAX) {
            uint8_t s = 0x5A;
            for (uint8_t i = 0; i < LEGACY_SIZE - 1U; i++) s = (uint8_t)(s + b[i] * 3U + i);
            if (b[LEGACY_SIZE - 1] == s) {
                char ssid[WSTORE_SSID_MAX + 1] = {0}, pwd[WSTORE_PWD_MAX + 1] = {0};
                memcpy(ssid, &b[3], b[1]);
                memcpy(pwd, &b[3 + 32], b[2]);
                if (WStore_Save(ssid, pwd)) {
                    uint8_t z[16] = {0};
                    ee_write(EEPROM_WIFI_ADDR, z, sizeof z);   /* old record is now redundant */
                }
            }
        }
    }
}

uint8_t WStore_Count(void) { return count; }

const wstore_net_t *WStore_Get(uint8_t index) { return index < count ? &ent[index].net : NULL; }

int WStore_Find(const char *ssid)
{
    if (!ssid) return -1;
    for (uint8_t i = 0; i < count; i++) if (strcmp(ent[i].net.ssid, ssid) == 0) return i;
    return -1;
}

bool WStore_Save(const char *ssid, const char *pwd)
{
    int k = WStore_Find(ssid);
    return WStore_SaveEx(ssid, pwd, k >= 0 ? ent[k].net.hidden : false);
}

bool WStore_SaveEx(const char *ssid, const char *pwd, bool hidden)
{
    if (!ssid || !ssid[0] || strlen(ssid) > WSTORE_SSID_MAX) return false;
    if (!pwd) pwd = "";
    if (strlen(pwd) > WSTORE_PWD_MAX) return false;

    int i = WStore_Find(ssid);
    if (i >= 0) {
        if (i == 0 && strcmp(ent[0].net.pwd, pwd) == 0 && ent[0].net.hidden == hidden) return true;           /* already stored and already the newest */
        entry_t e = ent[i];
        memset(e.net.pwd, 0, sizeof e.net.pwd);
        strcpy(e.net.pwd, pwd);
        e.net.hidden = hidden;
        renumber_if_needed();
        e.use = next_use();
        if (!write_slot(&e)) return false;
        ent[i] = e;
    } else {
        entry_t e;
        memset(&e, 0, sizeof e);
        strcpy(e.net.ssid, ssid);
        strcpy(e.net.pwd, pwd);
        e.net.hidden = hidden;
        if (count < WSTORE_MAX) {
            bool used[EEPROM_WIFILIST_SLOTS] = {0};
            for (uint8_t k = 0; k < count; k++) used[ent[k].slot] = true;
            for (uint8_t s = 0; s < EEPROM_WIFILIST_SLOTS; s++) if (!used[s]) { e.slot = s; break; }
            renumber_if_needed();
            e.use = next_use();
            if (!write_slot(&e)) return false;
            ent[count++] = e;
        } else {                                                                 /* full: replace the least recently used */
            e.slot = ent[count - 1].slot;
            renumber_if_needed();
            e.use = next_use();
            if (!write_slot(&e)) return false;
            ent[count - 1] = e;
        }
    }
    sort_entries();
    return true;
}

bool WStore_Forget(const char *ssid)
{
    int i = WStore_Find(ssid);
    if (i < 0) return false;
    erase_slot(ent[i].slot);
    for (uint8_t k = (uint8_t)i; k + 1U < count; k++) ent[k] = ent[k + 1];
    count--;
    return true;
}

bool WStore_Touch(const char *ssid)
{
    int i = WStore_Find(ssid);
    if (i < 0) return false;
    if (i == 0) return true;
    renumber_if_needed();
    entry_t e = ent[i];
    e.use = next_use();
    if (!write_slot(&e)) return false;
    ent[i] = e;
    sort_entries();
    return true;
}

uint8_t WStore_Candidates(const wstore_seen_t *seen, uint8_t n_seen, uint8_t *out, uint8_t max_out)
{
    int8_t  best_rssi[WSTORE_MAX];
    uint8_t order[WSTORE_MAX];
    uint8_t n = 0;
    for (uint8_t s = 0; s < count && n < WSTORE_MAX; s++) {      /* s is already in recency order */
        int8_t r = -127; bool found = false;
        for (uint8_t k = 0; k < n_seen; k++)
            if (seen[k].ssid && strcmp(seen[k].ssid, ent[s].net.ssid) == 0 && seen[k].rssi > r) { r = seen[k].rssi; found = true; }
        if (!found || r < WSTORE_MIN_RSSI) continue;
        /* insertion sort by signal (stable, so equal signals keep recency order) */
        uint8_t j = n;
        while (j > 0 && best_rssi[j - 1] < r) { best_rssi[j] = best_rssi[j - 1]; order[j] = order[j - 1]; j--; }
        best_rssi[j] = r; order[j] = s;
        n++;
    }
    if (n > max_out) n = max_out;
    for (uint8_t i = 0; i < n; i++) out[i] = order[i];
    return n;
}
