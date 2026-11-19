/**
  * @file    wifi_store.h
  * @brief   Remembers several Wi-Fi networks (name + password) in the AT24C32 EEPROM.
  *          Up to WSTORE_MAX networks. Index 0 is always the most recently used one.
  *          When the list is full the least recently used network is replaced.
  *          Passwords are stored as plain text in the EEPROM (it is a clock, not a bank).
  */
#ifndef WIFI_STORE_H
#define WIFI_STORE_H

#include <stdint.h>
#include <stdbool.h>

#define WSTORE_MAX       6
#define WSTORE_SSID_MAX  32
#define WSTORE_PWD_MAX   63

typedef struct {
    char ssid[WSTORE_SSID_MAX + 1];
    char pwd[WSTORE_PWD_MAX + 1];
    bool hidden;                 /* typed by hand and never shows up in scans: joined by name when nothing else is in range */
} wstore_net_t;

/* One network seen by a scan (only what the picker needs). */
typedef struct {
    const char *ssid;
    int8_t      rssi;
} wstore_seen_t;

void   WStore_Load(void);                         /* read the EEPROM (and import the old single-network record once) */
uint8_t WStore_Count(void);
const wstore_net_t *WStore_Get(uint8_t index);    /* 0 = most recently used; NULL if out of range */
int    WStore_Find(const char *ssid);             /* index or -1 */
bool   WStore_Save(const char *ssid, const char *pwd);   /* add or update, and make it the most recent (keeps the hidden flag) */
bool   WStore_SaveEx(const char *ssid, const char *pwd, bool hidden);
bool   WStore_Forget(const char *ssid);
bool   WStore_Touch(const char *ssid);            /* mark as most recently used (successful join with the stored password) */

/* Which saved networks should be tried, in order, given what a scan found?
 * Strongest signal first (weaker than WSTORE_MIN_RSSI is skipped); equal signal -> most recently used first.
 * Writes saved-list indexes into out[], returns how many. */
#define WSTORE_MIN_RSSI  (-88)
uint8_t WStore_Candidates(const wstore_seen_t *seen, uint8_t n_seen, uint8_t *out, uint8_t max_out);

#endif
