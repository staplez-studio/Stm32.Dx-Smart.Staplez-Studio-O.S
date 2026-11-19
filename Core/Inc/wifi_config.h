#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

/* ---- User settings for Wi-Fi, time sync and weather --------------------------------------- */

/* Join the saved network automatically a few seconds after power-up (1 = yes) */
#define WIFI_AUTOCONNECT          1
/* Wait this long after power-up before talking to the module: an ESP takes 1-3 s to start */
#define WIFI_BOOT_PROBE_DELAY_MS  2500U

/* Set the clock (time AND date) from the network whenever the module is online: right after it connects,
 * then every WIFI_TIME_SYNC_HOURS. If a sync fails it retries after 10 minutes.
 * WIFI_AUTOSYNC_REQUIRES_ZONE = 1 waits until a time zone has been saved once (Set Time > zone > SAVE), because
 * the built-in default (UTC-5, US/Canada rule) is only a guess and a wrong zone would shift the clock.
 * The SYNC button on the Wi-Fi screen always works. */
#define WIFI_AUTO_TIME_SYNC          1
#define WIFI_TIME_SYNC_HOURS         12
#define WIFI_AUTOSYNC_REQUIRES_ZONE  1

/* Fetch the weather automatically when online (after the time sync), then every WEATHER_REFRESH_MIN. */
#define WEATHER_AUTO_REFRESH         1

/* Network time source for "SYNC". The time zone itself is a setting on the clock (Set Time > zone). */
#define WIFI_NTP_SERVER           "pool.ntp.org"

/* Weather over plain HTTP from wttr.in (no account or key needed).
 * WEATHER_LOCATION "" = guess from your public IP address, or e.g. "Toronto" / "M5V" / "~Eiffel+Tower". */
#define WEATHER_HOST              "wttr.in"
#define WEATHER_LOCATION          ""
#define WEATHER_FAHRENHEIT        0
#define WEATHER_REFRESH_MIN       30

/* Where the saved Wi-Fi network lives in the EEPROM: see eeprom_map.h */
#include "eeprom_map.h"
#define WIFI_CREDS_EEPROM_ADDR    EEPROM_WIFI_ADDR

#endif
