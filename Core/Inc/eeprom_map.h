#ifndef EEPROM_MAP_H
#define EEPROM_MAP_H
/* Layout of the AT24C32 (4 KB) on the DS3231 board. Every record has its own region, 32-byte aligned
 * (one page), and the checks below fail the build if two of them ever overlap. */

#define EEPROM_SETTINGS_ADDR   0x0000U   /* NVRAM_Settings_t (18 bytes)                    */
#define EEPROM_SETTINGS_SIZE   32U
#define EEPROM_WIFI_ADDR       0x0100U   /* saved Wi-Fi network: 100 bytes                 */
#define EEPROM_WIFI_SIZE       128U
#define EEPROM_TZ_ADDR         0x0180U   /* time zone: 5 bytes                             */
#define EEPROM_TZ_SIZE         32U
#define EEPROM_TOTAL_SIZE      4096U

_Static_assert(EEPROM_SETTINGS_ADDR + EEPROM_SETTINGS_SIZE <= EEPROM_WIFI_ADDR, "EEPROM: settings overlap Wi-Fi");
_Static_assert(EEPROM_WIFI_ADDR + EEPROM_WIFI_SIZE <= EEPROM_TZ_ADDR,           "EEPROM: Wi-Fi overlaps time zone");
_Static_assert(EEPROM_TZ_ADDR + EEPROM_TZ_SIZE <= EEPROM_TOTAL_SIZE,            "EEPROM: time zone beyond the chip");
_Static_assert((EEPROM_WIFI_ADDR % 32U) == 0 && (EEPROM_TZ_ADDR % 32U) == 0,    "EEPROM: records must be page aligned");

#endif
