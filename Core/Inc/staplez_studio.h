/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    staplez_studio.h
  * @brief   Staplez Studio OS core architecture and state machine header.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __STAPLEZ_STUDIO_H__
#define __STAPLEZ_STUDIO_H__

#include <stdint.h>
#include "clock_zone.h"
#include <stdbool.h>

typedef enum {
    STATE_SPLASH = 0,
    STATE_MAIN_SCREEN,
    STATE_ICON_MENU,
    STATE_FAN_APP,
    STATE_SOUND_APP,
    STATE_WEATHER_APP,
    STATE_WIFI_APP,
    STATE_WIFI_PASSWORD,
    STATE_SET_TIME_APP,
    STATE_ENTERTAINMENT_APP,
    STATE_FILE_EXPLORER_APP,
    STATE_SYMBOLS_APP,
    STATE_ALARM_TIME_APP,
    STATE_ALARM_SINGLE_APP,
    STATE_ALARM_DOUBLE_APP,
    STATE_SCREEN_MOVER,
    STATE_IR_REMOTE_APP,
    STATE_CALENDAR_APP,
    STATE_TIMEZONE_APP
} SystemState_t;

extern SystemState_t current_state;
extern uint32_t state_timer;

extern uint16_t current_year;
extern uint8_t current_month;
extern uint8_t current_day_num;
extern uint8_t current_wday;
extern uint8_t current_hour;
extern uint8_t current_minute;
extern uint8_t current_second;
extern uint16_t current_ms;
extern uint8_t is_pm;

extern const char *months[13];
extern const char *week_days[7];

void Staplez_Init(void);
void Staplez_Process(void);

void DS3231_Init(void);
void DS3231_GetTime(void);
void DS3231_SetTime(uint8_t h, uint8_t m, uint8_t s, uint8_t pm, uint8_t d, uint8_t mo, uint16_t y);  /* values as shown on the clock (local, incl. DST) */
void DS3231_SetStdTime(const wnet_time_t *std);   /* standard time, e.g. from the network */
void TZ_Load(void);                               /* time zone settings from EEPROM (defaults if none saved) */
bool TZ_Save(void);
extern bool tz_dst_now;                           /* daylight saving is in effect right now */
extern int16_t tz_offset_now_min;                 /* UTC offset in force right now, in minutes */
extern wnet_time_t tz_std_now;                    /* the DS3231 time (standard time) */
void DS3231_SaveAllSettings(void);
void DS3231_LoadAllSettings(void);
void EEPROM_SaveSettings(void);
uint8_t calculate_day_of_week(uint16_t y, uint8_t m, uint8_t d);
extern bool ds3231_present;
extern volatile uint8_t rtc_sqw_pending;   /* set by the SQW EXTI ISR, consumed in Staplez_Process() */
extern bool rtc_already_configured;
void increment_software_time(void);
void set_system_state(SystemState_t state);

#endif
