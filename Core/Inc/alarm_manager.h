/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    alarm_manager.h
  * @brief   Dual Alarm Manager with EEPROM persistence and tone rhythms.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __ALARM_MANAGER_H__
#define __ALARM_MANAGER_H__

#include <stdint.h>
#include "main.h"
#include "sound_app.h"

#define NUM_ALARMS 1
#define EEPROM_ADDRESS (0x57 << 1)

#define BUZZER_MELODY_BASIC          0
#define BUZZER_MELODY_STANDARD       1
#define BUZZER_MELODY_POPCORN        2
#define BUZZER_MELODY_WAKEUP         3
#define BUZZER_MELODY_ACTIVE_PULSE   4
#define BUZZER_MELODY_SHEET_MUSIC    5

#ifndef I2C_MEMADD_SIZE_8BIT
#define I2C_MEMADD_SIZE_8BIT            0x00000001U
#endif
#ifndef I2C_MEMADD_SIZE_16BIT
#define I2C_MEMADD_SIZE_16BIT           0x00000002U
#endif

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t is_pm;
    uint8_t rhythm_type;   // 1 = Single tone (Buzzer), 2 = Double tone (DFPlayer SD MP3)
    uint8_t buzzer_melody; // 0 = Sheet Music, 1 = Wake Klaxon, 2 = Popcorn, 3 = Standard Beep
    uint8_t enabled;       // 1 = Armed/ON, 0 = OFF
    uint8_t mp3_track;     // DFPlayer track number played by the double-tone alarm (1..)
} AlarmConfig_t;

void Buzzer_Beep(uint16_t freq, uint16_t duration);
void DFPlayer_PlayTrack(uint8_t trackNumber);

extern AlarmConfig_t alarms[NUM_ALARMS];
extern uint8_t current_alarm_index;
extern uint8_t is_sdcard_inserted;
extern uint8_t is_alarm_test_playing;

void Alarm_Manager_Init(void);
void EEPROM_SaveAlarms(void);
void EEPROM_LoadAlarms(void);
uint8_t Any_Alarm_Enabled(void);
void Alarm_Manager_Process(char key, uint32_t current_tick);
void Alarm_PlayTestMelody(void);
void Alarm_StopTestMelody(void);
void Alarm_ToggleTestMelody(void);

extern uint8_t is_alarm_ringing;
void Alarm_Check_Process(uint32_t current_tick);
void Alarm_Dismiss(void);

#endif
