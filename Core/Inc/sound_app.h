/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    sound_app.h
  * @brief   Master Volume & 5-Band Equalizer (31Hz, 62Hz, 125Hz, 250Hz, 500Hz).
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __SOUND_APP_H__
#define __SOUND_APP_H__

#include <stdint.h>
#include <stdbool.h>

#define ALARM_SOUND_MODE          1

void IR_Remote_Check_Timeout(void);

#define BUZZER_MELODY_BASIC          0
#define BUZZER_MELODY_STANDARD       1
#define BUZZER_MELODY_POPCORN        2
#define BUZZER_MELODY_WAKEUP         3
#define BUZZER_MELODY_ACTIVE_PULSE   4
#define BUZZER_MELODY_SHEET_MUSIC    5

void Sound_App_Init(void);
void Sound_App_Process(char key, uint32_t current_tick);
void Sound_App_Render(void);

void Buzzer_Beep(uint16_t freq, uint16_t duration);
void Buzzer_Tone(uint16_t freq, uint16_t duration_ms);
void Buzzer_Stop(void);
void ActiveBuzzer_On(void);
void ActiveBuzzer_Off(void);
void Splash_Melody_Process(void);
void Sound_PlaySelectedAlarmMelody(uint8_t melody_id);
void Alarm_Test_Melody_Process(void);
void Alarm_Ringing_Process(uint32_t current_tick);
void DFPlayer_SendCommand(uint8_t cmd, uint16_t arg);
void DFPlayer_PlayTrack(uint8_t trackNumber);
void DFPlayer_SetVolume(uint8_t vol);
void DFPlayer_SetEQ(uint8_t eq);
void DFPlayer_Pause(void);
void DFPlayer_Play(void);
void DFPlayer_NextTrack(void);
void DFPlayer_PrevTrack(void);
/* Real list of what the DFPlayer can read: the number of MP3 files on ITS OWN micro SD card, asked with
 * command 0x48. The module cannot report file names, so tracks are listed by number (0001, 0002 ...).
 * Needs the DFPlayer TX pin wired to PA1 (see README_SD_AND_DFPLAYER.txt). */
bool     DFPlayer_RefreshTrackCount(void);   /* asks the module; false = no answer */
bool     DFPlayer_TrackCountKnown(void);
uint16_t DFPlayer_GetTrackCount(void);
const char *DFPlayer_LastError(void);        /* short text for the screen when the count is unknown */
void     DFPlayer_SetRxHook(bool (*fn)(uint8_t *byte, uint32_t timeout_us));   /* tests only */
void Sound_Apply_Hardware(void);
void Sound_Apply_Hardware_Force(void);   /* also resends DFPlayer volume/EQ */
uint8_t Sound_App_GetVolume(void);   /* master volume 0..13 for other screens */
bool ADAU1401_IsDetected(void);
void Sound_App_SetReturnState(uint8_t return_state);
uint8_t Sound_App_GetReturnState(void);

#endif
