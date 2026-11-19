/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    staplez_studio.c
  * @brief   Staplez Studio UI - Fully modularized IR-Controlled master OS.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "staplez_studio.h"
#include "clock_zone.h"
#include "eeprom_map.h"
#include "calendar_app.h"
#include "timezone_app.h"
#include "wifi_manager.h"
#include "alarm_manager.h"
#include "time_manager.h"
#include "fan_app.h"
#include "sound_app.h"
#include "ir_remote_app.h"
#include "entertainment_app.h"
#include "hard_drive_app.h"
#include "weather_app.h"
#include "app_menu.h"
#include "main_screen.h"
#include "sh1106.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "weather_icons.h"

extern I2C_HandleTypeDef hi2c1;
#define DS3231_ADDRESS (0x68 << 1)

SystemState_t current_state = STATE_SPLASH;
uint32_t state_timer = 0;

void set_system_state(SystemState_t state)
{
    /* The Sound screen's BACK goes to whichever screen opened it: the MP3 player when opened from
     * Entertainment, otherwise the icon menu. Decided here so it can never be left stale. */
    if (state == STATE_SOUND_APP && current_state != STATE_SOUND_APP) {
        Sound_App_SetReturnState((current_state == STATE_ENTERTAINMENT_APP) ? STATE_ENTERTAINMENT_APP
                                                                           : STATE_ICON_MENU);
    }
    current_state = state;
    state_timer = HAL_GetTick();
}

uint16_t current_year = 2026;
uint8_t current_month = 10;
uint8_t current_day_num = 24;
uint8_t current_wday = 6;
uint8_t current_hour = 12;
uint8_t current_minute = 0;
uint8_t current_second = 0;
uint16_t current_ms = 0;
uint8_t is_pm = 1;

const char *months[13] = {
    "", "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

const char *week_days[7] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

static uint32_t last_ds3231_poll = 0;
static uint32_t last_heartbeat = 0;

/* With the SQW interrupt the 500 ms poll is only a safety net. Without it (DX-SMART PJ26: PA10 is
 * the CH340X RX line) poll faster so the seconds digit follows the DS3231 closely. Each read is
 * ~0.5 ms of bus time at the patched I2C speed. */
#if RTC_SQW_ENABLED
#define RTC_POLL_MS 500
#else
#define RTC_POLL_MS 200
#endif
static uint32_t last_sec_tick = 0;

static uint8_t bcd_to_dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t dec_to_bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

uint8_t calculate_day_of_week(uint16_t y, uint8_t m, uint8_t d)
{
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

void increment_software_time(void)
{
    current_second++;
    if (current_second >= 60) {
        current_second = 0;
        current_minute++;
        if (current_minute >= 60) {
            current_minute = 0;
            current_hour++;
            if (current_hour > 12) current_hour = 1;
            else if (current_hour == 12) is_pm = !is_pm;
        }
    }
}

bool ds3231_present = false;
volatile uint8_t rtc_sqw_pending = 0;
bool tz_dst_now = false;
int16_t tz_offset_now_min = TZ_DEFAULT_OFFSET_MIN;
wnet_time_t tz_std_now = { 2026, 1, 1, 0, 0, 0, 4 };
bool rtc_already_configured = false;
#define BKP_RTC_CONFIGURED_MAGIC 0xA5A5

void DS3231_Init(void)
{
    // 1. Enable PWR and BKP peripheral clocks for Backup Domain access
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    PWR->CR |= (1U << 8); // Disable Backup domain write protection (PWR_CR_DBP)

    // 2. Allow DS3231 power transition / VCC to settle after USB plug-in
    HAL_Delay(35);

    // 3. Retry connection up to 10 times to ensure DS3231 is ready on I2C
    bool ready = false;
    for (uint8_t attempt = 0; attempt < 10; attempt++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, DS3231_ADDRESS, 2, 35) == HAL_OK) {
            ready = true;
            break;
        }
        HAL_Delay(10);
    }

    if (!ready) {
        ds3231_present = false;
        return;
    }
    ds3231_present = true;

    // 4. Ensure DS3231 Control Register (0x0E):
    // Bit 7 EOSC = 0 (Oscillator Enabled on Battery Backup power!)
    // Bit 6 BBSQW = 1 (Battery-Backed Square-Wave enable)
    // Bit 2 INTCN = 0 (Output Square Wave on SQW/INT pin)
    // Bits 4,3 RS2,RS1 = 0,0 (1 Hz square wave)
    uint8_t ctrl = 0x40; // EOSC=0, BBSQW=1, RS2=0, RS1=0, INTCN=0 -> 1Hz square wave always on
    HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDRESS, 0x0E, 1, &ctrl, 1, 50);

    // 5. Read DS3231 Status Register (0x0F) and clear OSF flag if set
    uint8_t status_reg = 0x00;
    if (HAL_I2C_Mem_Read(&hi2c1, DS3231_ADDRESS, 0x0F, 1, &status_reg, 1, 50) == HAL_OK) {
        if (status_reg & 0x80) {
            status_reg &= ~0x80; // Clear Oscillator Stop Flag
            HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDRESS, 0x0F, 1, &status_reg, 1, 50);
        }
    }

    // 6. Read live time from DS3231 hardware registers.
    // NEVER overwrite the hardware RTC registers on boot!
    DS3231_GetTime();
}

void DS3231_GetTime(void)
{
    uint8_t buf[7];
    if (HAL_I2C_Mem_Read(&hi2c1, DS3231_ADDRESS, 0x00, 1, buf, 7, 50) == HAL_OK) {
        ds3231_present = true;
        uint8_t s = bcd_to_dec(buf[0] & 0x7F);
        uint8_t m = bcd_to_dec(buf[1]);
        uint8_t raw_h = buf[2];
        uint8_t h, pm;
        if (raw_h & 0x40) { // 12-hour mode
            pm = (raw_h & 0x20) ? 1 : 0;
            h = bcd_to_dec(raw_h & 0x1F);
        } else { // 24-hour mode
            uint8_t h24 = bcd_to_dec(raw_h & 0x3F);
            pm = (h24 >= 12) ? 1 : 0;
            h = h24 % 12;
            if (h == 0) h = 12;
        }
        if (h >= 1 && h <= 12 && m < 60 && s < 60) {
            uint8_t d = bcd_to_dec(buf[4]);
            uint8_t mo = bcd_to_dec(buf[5] & 0x1F);
            uint16_t y = 2000 + bcd_to_dec(buf[6]);
            if (d >= 1 && d <= 31 && mo >= 1 && mo <= 12 && y >= 1980 && y <= 2099 && d <= tz_days_in_month(y, mo)) {
                /* The DS3231 keeps STANDARD time. Add daylight saving here so it works with no internet. */
                wnet_time_t std = { y, mo, d, (uint8_t)((h % 12) + (pm ? 12 : 0)), m, s, 0 }, loc;
                bool dst;
                tz_std_to_local(&tz_config, &std, &loc, &dst);
                current_year = loc.year;
                current_month = loc.month;
                current_day_num = loc.day;
                current_wday = loc.wday;
                current_second = loc.second;
                current_minute = loc.minute;
                current_hour = (uint8_t)((loc.hour % 12U) ? (loc.hour % 12U) : 12U);
                is_pm = (loc.hour >= 12) ? 1 : 0;
                tz_dst_now = dst;
                tz_std_now = std;
                tz_offset_now_min = (int16_t)(tz_config.std_offset_min + (dst ? 60 : 0));
            } else {
                /* implausible date registers: show the time of day only */
                current_second = s;
                current_minute = m;
                current_hour = h;
                is_pm = pm;
            }
        }
    } else {
        ds3231_present = false;
    }
}

static void ds3231_write_std(const wnet_time_t *std)
{
    uint8_t h12 = (uint8_t)(std->hour % 12U);
    if (h12 == 0) h12 = 12;
    uint8_t pm = (std->hour >= 12) ? 1 : 0;
    uint8_t buf[7];
    buf[0] = dec_to_bcd(std->second) & 0x7F; // CH = 0
    buf[1] = dec_to_bcd(std->minute);
    buf[2] = 0x40 | (pm ? 0x20 : 0x00) | dec_to_bcd(h12); // 12h mode
    buf[3] = dec_to_bcd((uint8_t)(calculate_day_of_week(std->year, std->month, std->day) + 1));
    buf[4] = dec_to_bcd(std->day);
    buf[5] = dec_to_bcd(std->month);
    buf[6] = dec_to_bcd((uint8_t)(std->year % 100));
    HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDRESS, 0x00, 1, buf, 7, 50);
}

void DS3231_SetStdTime(const wnet_time_t *std)
{
    ds3231_write_std(std);
}

/* h/m/s/pm/d/mo/y are what the clock displays, i.e. local time including daylight saving.
 * The chip stores standard time, so convert first. */
void DS3231_SetTime(uint8_t h, uint8_t m, uint8_t s, uint8_t pm, uint8_t d, uint8_t mo, uint16_t y)
{
    wnet_time_t loc = { y, mo, d, (uint8_t)((h % 12) + (pm ? 12 : 0)), m, s, 0 }, std;
    tz_local_to_std(&tz_config, &loc, &std);
    ds3231_write_std(&std);
}

/* ---- time zone setting, kept in the AT24C32 next to the other settings ---- */
#define TZ_EEPROM_ADDR   EEPROM_TZ_ADDR   /* see eeprom_map.h: clear of settings and the Wi-Fi record */
#define TZ_MAGIC         0xB3

static uint8_t tz_crc(uint8_t rule, int16_t off)
{
    return (uint8_t)(0xA5 ^ rule ^ (uint8_t)(off & 0xFF) ^ (uint8_t)((off >> 8) & 0xFF));
}

void TZ_Load(void)
{
    uint8_t b[5];
    tz_config.std_offset_min = TZ_DEFAULT_OFFSET_MIN;
    tz_config.rule = TZ_DEFAULT_RULE;
    tz_saved = false;
    if (HAL_I2C_Mem_Read(&hi2c1, (0x57 << 1), TZ_EEPROM_ADDR, I2C_MEMADD_SIZE_16BIT, b, sizeof(b), 100) != HAL_OK) return;
    if (b[0] != TZ_MAGIC) return;
    int16_t off = (int16_t)((uint16_t)b[2] | ((uint16_t)b[3] << 8));
    if (b[1] >= TZ_DST_RULE_COUNT || off < TZ_OFFSET_MIN_LIMIT || off > TZ_OFFSET_MAX_LIMIT) return;
    if (b[4] != tz_crc(b[1], off)) return;
    tz_config.std_offset_min = off;
    tz_config.rule = b[1];
    tz_saved = true;
}

bool TZ_Save(void)
{
    uint8_t b[5];
    b[0] = TZ_MAGIC;
    b[1] = tz_config.rule;
    b[2] = (uint8_t)(tz_config.std_offset_min & 0xFF);
    b[3] = (uint8_t)((tz_config.std_offset_min >> 8) & 0xFF);
    b[4] = tz_crc(tz_config.rule, tz_config.std_offset_min);
    bool ok = HAL_I2C_Mem_Write(&hi2c1, (0x57 << 1), TZ_EEPROM_ADDR, I2C_MEMADD_SIZE_16BIT, b, sizeof(b), 100) == HAL_OK;
    HAL_Delay(12);   /* AT24C32 write cycle */
    if (ok) tz_saved = true;
    return ok;
}

#define AT24C32_EEPROM_ADDR (0x57 << 1)
#define NVRAM_SETTINGS_MAGIC 0x535A // 'S' 'Z'

typedef struct {
    uint16_t magic;
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  is_pm;
    uint8_t  column_offset;
    uint8_t  alarm_enabled;
    uint8_t  alarm_hour;
    uint8_t  alarm_minute;
    uint8_t  alarm_is_pm;
    uint8_t  alarm_rhythm;
    uint8_t  alarm_buzzer_melody;
    uint8_t  is_sdcard_inserted;
    uint8_t  alarm_mp3_track;     /* added later: old records hold 0xFF here, read as track 1 */
} NVRAM_Settings_t;

_Static_assert(sizeof(NVRAM_Settings_t) <= EEPROM_SETTINGS_SIZE, "EEPROM: settings record outgrew its 32 byte page");

void EEPROM_SaveSettings(void)
{
    NVRAM_Settings_t cfg;
    cfg.magic = NVRAM_SETTINGS_MAGIC;
    cfg.year = current_year;
    cfg.month = current_month;
    cfg.day = current_day_num;
    cfg.hour = current_hour;
    cfg.minute = current_minute;
    cfg.second = current_second;
    cfg.is_pm = is_pm;
    cfg.column_offset = sh1106_column_offset;
    cfg.alarm_enabled = alarms[0].enabled;
    cfg.alarm_hour = alarms[0].hour;
    cfg.alarm_minute = alarms[0].minute;
    cfg.alarm_is_pm = alarms[0].is_pm;
    cfg.alarm_rhythm = alarms[0].rhythm_type;
    cfg.alarm_buzzer_melody = alarms[0].buzzer_melody;
    cfg.is_sdcard_inserted = is_sdcard_inserted;
    cfg.alarm_mp3_track = alarms[0].mp3_track;

    HAL_I2C_Mem_Write(&hi2c1, AT24C32_EEPROM_ADDR, EEPROM_SETTINGS_ADDR, I2C_MEMADD_SIZE_16BIT, (uint8_t*)&cfg, sizeof(cfg), 100);
}

void DS3231_SaveAllSettings(void)
{
    // Called ONLY when time/date is explicitly saved by the user in the Time Manager app
    DS3231_SetTime(current_hour, current_minute, current_second, is_pm, current_day_num, current_month, current_year);
    EEPROM_SaveSettings();
}

void DS3231_LoadAllSettings(void)
{
    // Read live battery-backed RTC time from DS3231 registers
    if (ds3231_present) {
        DS3231_GetTime();
    }

    NVRAM_Settings_t cfg;
    if (HAL_I2C_Mem_Read(&hi2c1, AT24C32_EEPROM_ADDR, EEPROM_SETTINGS_ADDR, I2C_MEMADD_SIZE_16BIT, (uint8_t*)&cfg, sizeof(cfg), 100) == HAL_OK) {
        if (cfg.magic == NVRAM_SETTINGS_MAGIC) {
            sh1106_column_offset = (cfg.column_offset <= 4) ? cfg.column_offset : 0;
            alarms[0].enabled = cfg.alarm_enabled ? 1 : 0;
            alarms[0].hour = (cfg.alarm_hour >= 1 && cfg.alarm_hour <= 12) ? cfg.alarm_hour : 12;
            alarms[0].minute = (cfg.alarm_minute < 60) ? cfg.alarm_minute : 0;
            alarms[0].is_pm = cfg.alarm_is_pm ? 1 : 0;
            alarms[0].rhythm_type = (cfg.alarm_rhythm == 2) ? 2 : 1;
            alarms[0].buzzer_melody = (cfg.alarm_buzzer_melody <= 4) ? cfg.alarm_buzzer_melody : 0;
            is_sdcard_inserted = cfg.is_sdcard_inserted ? 1 : 0;
            alarms[0].mp3_track = (cfg.alarm_mp3_track >= 1 && cfg.alarm_mp3_track < 0xFF) ? cfg.alarm_mp3_track : 1;
            return;
        }
    }

    // Default settings if EEPROM is uninitialized
    sh1106_column_offset = 0;
    alarms[0].enabled = 0;
    alarms[0].hour = 12;
    alarms[0].minute = 0;
    alarms[0].is_pm = 0;
    alarms[0].rhythm_type = 1;
    alarms[0].buzzer_melody = 0;
    alarms[0].mp3_track = 1;
    is_sdcard_inserted = 1;

    // Save default EEPROM settings WITHOUT overwriting the battery-backed DS3231 time registers!
    EEPROM_SaveSettings();
}

void Staplez_Init(void)
{
    // Visual Boot confirmation: pulse the status LED
    LED_ON();
    HAL_Delay(100);
    LED_OFF();

    // Enable DWT cycle counter for precise microsecond buzzer delays
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    }
    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

    // Ensure buzzer pin PB5 starts in silent High-Z Tri-State mode
    Buzzer_Stop();

    sh1106_init();

    // Pristine Splash Screen rendered immediately on power-up:
    memcpy(sh1106_buffer, pixel_art_grid_1792549062222, sizeof(sh1106_buffer));
    sh1106_update();

    TZ_Load();
    DS3231_Init();
    DS3231_GetTime();
    EEPROM_LoadAlarms();

    current_state = STATE_SPLASH;
    state_timer = HAL_GetTick();
    last_ds3231_poll = HAL_GetTick();
    last_sec_tick = HAL_GetTick();

    Main_Screen_Init();
    App_Menu_Init();
    WiFi_Manager_Init();
    Alarm_Manager_Init();
    Fan_App_Init();
    Sound_App_Init();
    Ir_Remote_App_Init();
    Entertainment_App_Init();

    // Ensure splash screen is updated after initialization
    memcpy(sh1106_buffer, pixel_art_grid_1792549062222, sizeof(sh1106_buffer));
    sh1106_update();
}

void Staplez_Process(void)
{
    uint32_t current_tick = HAL_GetTick();
    current_ms = current_tick % 1000;

    // Check Alarm condition continuously while power is on
    Alarm_Check_Process(current_tick);

    // Push saved volume / EQ to the DFPlayer and the ADAU once, shortly after power-up
    #define AUDIO_HW_BOOT_DELAY_MS 2500U
    static uint8_t audio_hw_applied = 0;
    if (!audio_hw_applied && current_tick >= AUDIO_HW_BOOT_DELAY_MS) {
        audio_hw_applied = 1;
        Sound_Apply_Hardware();
    }

    // Start the Wi-Fi module a few seconds after power-up and join the saved network
    WiFi_Manager_Background(current_tick);

    // 1 Hz SQW edge from the DS3231: read the RTC here (main context), never in the ISR.
    // Ignored while the Set Time screen is open so live seconds never interfere with editing.
    if (rtc_sqw_pending) {
        rtc_sqw_pending = 0;
        if (current_state != STATE_SET_TIME_APP && ds3231_present) {
            DS3231_GetTime();
        }
    }

    // Sync DS3231 RTC every 500ms if hardware RTC module is present.
    // PAUSED while on the Set Time screen so live seconds/minutes never interfere with editing!
    if (current_state != STATE_SET_TIME_APP) {
        if (ds3231_present && (current_tick - last_ds3231_poll >= RTC_POLL_MS)) {
            last_ds3231_poll = current_tick;
            DS3231_GetTime();
            if (current_tick - last_heartbeat >= 500) {
                last_heartbeat = current_tick;
                LED_TOGGLE(); // Heartbeat blink (1 Hz), independent of the RTC poll rate
            }
        } else if (!ds3231_present && (current_tick - last_ds3231_poll >= 1000)) {
            // Attempt hot-plug / delayed boot re-detection if RTC was initializing
            last_ds3231_poll = current_tick;
            DS3231_Init();
        }

        // Software 1-second tick backup if hardware RTC is not connected
        if (!ds3231_present && (current_tick - last_sec_tick >= 1000)) {
            last_sec_tick += 1000;
            increment_software_time();
            LED_TOGGLE(); // Heartbeat blink
        }
    }

    // Process IR Remote signals everywhere (100% IR-only control architecture)
    IR_Remote_Update();

    switch (current_state)
    {
        case STATE_SPLASH:
            Splash_Melody_Process();
            // Auto-boot to Main Screen after 2.5 seconds (2500ms) so display is NEVER frozen
            if ((current_tick - state_timer) >= 2500) {
                Buzzer_Stop();
                set_system_state(STATE_MAIN_SCREEN);
                Main_Screen_Init();
                Main_Screen_Render();
            }
            break;

        case STATE_MAIN_SCREEN:
            Main_Screen_Process(0, current_tick);
            break;

        case STATE_ICON_MENU:
            App_Menu_Process(0, current_tick);
            break;

        case STATE_WIFI_APP:
        case STATE_WIFI_PASSWORD:
            WiFi_Manager_Process(0, current_tick);
            break;

        case STATE_SET_TIME_APP:
            Time_Manager_Process(0, current_tick);
            break;

        case STATE_CALENDAR_APP:
            Calendar_App_Process(0, current_tick);
            break;

        case STATE_TIMEZONE_APP:
            Timezone_App_Process(0, current_tick);
            break;

        case STATE_ALARM_TIME_APP:
        case STATE_ALARM_SINGLE_APP:
        case STATE_ALARM_DOUBLE_APP:
            Alarm_Manager_Process(0, current_tick);
            break;

        case STATE_FAN_APP:
            Fan_App_Process(0, current_tick);
            break;

        case STATE_SOUND_APP:
            Sound_App_Process(0, current_tick);
            break;

        case STATE_IR_REMOTE_APP:
            Ir_Remote_App_Process(0, current_tick);
            break;

        case STATE_ENTERTAINMENT_APP:
            Entertainment_App_Process(0, current_tick);
            break;

        case STATE_FILE_EXPLORER_APP:
            Hard_Drive_App_Process(0, current_tick);
            break;

        case STATE_WEATHER_APP:
            Weather_App_Process(0, current_tick);
            break;

        default:
            current_state = STATE_MAIN_SCREEN;
            state_timer = current_tick;
            break;
    }
}
