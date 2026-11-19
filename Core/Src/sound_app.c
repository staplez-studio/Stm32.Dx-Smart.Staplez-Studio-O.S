/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    sound_app.c
  * @brief   Sound App UI matching emulator renderSoundApp to a tee.
  *          Features 16x32 barfill vertical sliders & 5-Band Equalizer.
  *          Controlled 100% via IR Remote (0x72C8 Left, 0x72D8 Right, 0xF2F0 Up,
  *          0xF2E0 Down, 0x7158 Play/Select, 0x7198 Shift/Back).
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "sound_app.h"
#include "dfplayer_proto.h"
#include "alarm_manager.h"
#include "staplez_studio.h"
#include "ir_remote_app.h"
#include "main.h"
#include "i2c.h"
#include "adau_params.h"
#include "sh1106.h"
#include "ui_draw.h"
#include "weather_icons.h"
#include <stdio.h>
#include <stdbool.h>

#define NOTE_REST  0
#define NOTE_D5    587
#define NOTE_CS5   554
#define NOTE_FS5   740
#define NOTE_G5    784
#define NOTE_A5    880
#define NOTE_C6    1047
#define NOTE_CS6   1109
#define NOTE_D6    1175
#define NOTE_DS6   1245
#define NOTE_E6    1319
#define NOTE_F6    1397
#define NOTE_G6    1568
#define NOTE_GS6   1661
#define NOTE_A6    1760
#define NOTE_AS6   1865
#define NOTE_B6    1975
#define NOTE_C7    2093
#define NOTE_D7    2349

typedef struct {
    uint16_t freq;
    uint8_t dur;
} MelodyNote_t;

typedef struct {
    uint16_t freq;
    uint16_t durMs;
    uint16_t gapMs;
} SheetNote_t;

/* Basic Blue Pill Startup Chime: 3 crisp, pleasant ascending tones (C6 -> E6 -> G6) */
static const SheetNote_t BASIC_BLUEPILL_CHIME[] = {
    { 1046, 75, 25 },  // C6
    { 1318, 75, 25 },  // E6
    { 1568, 140, 40 }  // G6
};
#define BASIC_CHIME_LENGTH (sizeof(BASIC_BLUEPILL_CHIME) / sizeof(BASIC_BLUEPILL_CHIME[0]))

/* Upbeat B-minor Popcorn Melody (Gershon Kingsley classic theme - octave shifted for pure piezo resonance) */
static const SheetNote_t POPCORN_MELODY[] = {
    // Measure 1
    { 1975, 160, 40 },  // B6
    { 1760, 160, 40 },  // A6
    { 1975, 160, 40 },  // B6
    { 1480, 160, 40 },  // F#6
    { 1175, 160, 40 },  // D6
    { 1480, 160, 40 },  // F#6
    { 988,  320, 60 },  // B5
    // Measure 2
    { 1975, 160, 40 },  // B6
    { 1760, 160, 40 },  // A6
    { 1975, 160, 40 },  // B6
    { 1480, 160, 40 },  // F#6
    { 1175, 160, 40 },  // D6
    { 1480, 160, 40 },  // F#6
    { 988,  320, 60 },  // B5
    // Measure 3
    { 1975, 160, 40 },  // B6
    { 2217, 160, 40 }, // C#7
    { 2349, 160, 40 }, // D7
    { 2217, 160, 40 }, // C#7
    { 2349, 160, 40 }, // D7
    { 1975, 160, 40 },  // B6
    { 2217, 160, 40 }, // C#7
    { 1975, 160, 40 },  // B6
    { 2217, 160, 40 }, // C#7
    { 1760, 160, 40 },  // A6
    // Measure 4
    { 1975, 160, 40 },  // B6
    { 1760, 160, 40 },  // A6
    { 1975, 160, 40 },  // B6
    { 1480, 160, 40 },  // F#6
    { 1975, 360, 80 }   // B6
};
#define POPCORN_MELODY_LENGTH (sizeof(POPCORN_MELODY) / sizeof(POPCORN_MELODY[0]))

/* Piercing Wake-Up Alarm: 2.75 kHz piezo mechanical resonance peak + 3.2 kHz dissonance bursts */
static const MelodyNote_t WAKEUP_ALARM_MELODY[] = {
    // Phase 1: 4 sharp, high-urgency bursts at 2,750 Hz
    {2750, 14}, {NOTE_REST, 35},
    {2750, 14}, {NOTE_REST, 35},
    {2750, 14}, {NOTE_REST, 35},
    {3200, 7},  {NOTE_REST, 14},

    // Phase 2: 4 sharp, high-urgency bursts
    {2750, 14}, {NOTE_REST, 35},
    {2750, 14}, {NOTE_REST, 35},
    {2750, 14}, {NOTE_REST, 35},
    {3200, 7},  {NOTE_REST, 14},

    // Phase 3: Alternating dual-frequency emergency warble
    {2450, 12}, {3150, 12},
    {2450, 12}, {3150, 12},
    {2450, 12}, {3300, 6},
    {NOTE_REST, 8}
};
#define WAKEUP_ALARM_LENGTH (sizeof(WAKEUP_ALARM_MELODY) / sizeof(WAKEUP_ALARM_MELODY[0]))

/* Transcribed Sheet Music Melody (Octave shifted for pure high notes matching startup chime) */
static const SheetNote_t SHEET_MUSIC_MELODY[] = {
    { 1975, 240, 40 },
    { 1760, 240, 40 },
    { 1975, 240, 40 },
    { 1480, 240, 40 },
    { 1318, 110, 30 },
    { 1480, 360, 50 },
    { 1975, 240, 40 },
    { 1760, 240, 40 },
    { 1975, 240, 40 },
    { 1480, 240, 40 },
    { 1318, 110, 30 },
    { 1480, 360, 50 },
    { 1975, 240, 40 },
    { 2217, 240, 40 },
    { 2349, 240, 40 },
    { 2217, 240, 40 },
    { 2349, 360, 50 },
    { 0,    0,   140 },
    { 1975, 110, 30 },
    { 2217, 240, 40 },
    { 1975, 110, 30 },
    { 2217, 360, 50 },
    { 1760, 110, 30 },
    { 1975, 240, 40 },
    { 1760, 240, 40 },
    { 1975, 240, 40 },
    { 1568, 240, 40 },
    { 1975, 500, 50 },
    { 1975, 240, 40 },
    { 1760, 180, 40 }
};
#define SHEET_MUSIC_LENGTH (sizeof(SHEET_MUSIC_MELODY) / sizeof(SHEET_MUSIC_MELODY[0]))

static uint8_t master_volume = 11; // 0..13
static uint8_t eq_bass = 8;        // 0..13
static uint8_t eq_mid = 7;         // 0..13
static uint8_t eq_treble = 9;      // 0..13
static bool is_muted = false;

void ActiveBuzzer_On(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = ACTIVE_BUZZER_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ACTIVE_BUZZER_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(ACTIVE_BUZZER_GPIO_Port, ACTIVE_BUZZER_Pin, GPIO_PIN_SET);
}

void ActiveBuzzer_Off(void)
{
    HAL_GPIO_WritePin(ACTIVE_BUZZER_GPIO_Port, ACTIVE_BUZZER_Pin, GPIO_PIN_RESET);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = ACTIVE_BUZZER_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ACTIVE_BUZZER_GPIO_Port, &GPIO_InitStruct);
}

void Buzzer_Stop(void)
{
    /* 1. Clear output data latches to 0V for both Passive (PB5) and Active (PB6) buzzers */
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ACTIVE_BUZZER_GPIO_Port, ACTIVE_BUZZER_Pin, GPIO_PIN_RESET);

    /* 2. Disconnect internal push-pull drivers: switch PB5 & PB6 into High-Impedance Input (Tri-State, No-Pull). */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = BUZZER_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BUZZER_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ACTIVE_BUZZER_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ACTIVE_BUZZER_GPIO_Port, &GPIO_InitStruct);
}

/**
 * Cycle-accurate microsecond delay using Cortex-M3 DWT cycle counter (72MHz).
 */
static void Buzzer_Delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000UL);
    while ((DWT->CYCCNT - start) < ticks) {
        // busy wait
    }
}

/**
 * Basic tone generator for buzzers.
 * Dynamically enables Push-Pull output on dual buzzer pins (PB5 & PB6) only during tone generation,
 * using inverted initial phase setup for differential voltage boost (2x peak-to-peak volume).
 * Returns both pins to silent High-Z Tri-State immediately after tone completes.
 * Polling IR_Remote_Check_Timeout ensures responsiveness while blocking.
 */
void Buzzer_Tone(uint16_t freq, uint16_t duration_ms)
{
    ActiveBuzzer_Off();

    if (freq == 0 || duration_ms == 0) {
        Buzzer_Stop();
        if (duration_ms > 0) {
            uint32_t start = HAL_GetTick();
            while ((HAL_GetTick() - start) < duration_ms) {
                IR_Remote_Check_Timeout();
                if (IR_Is_New_Frame_Ready()) return;
            }
        }
        return;
    }

    // Configure PB5 (BUZZER_Pin, Passive) as Push-Pull Output
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = BUZZER_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BUZZER_GPIO_Port, &GPIO_InitStruct);

    uint32_t half_period_us = 1000000UL / (2UL * (uint32_t)freq);
    if (half_period_us == 0) half_period_us = 1;
    uint32_t total_toggles = ((uint32_t)duration_ms * 1000UL) / half_period_us;

    for (uint32_t i = 0; i < total_toggles; i++) {
        HAL_GPIO_TogglePin(BUZZER_GPIO_Port, BUZZER_Pin);
        Buzzer_Delay_us(half_period_us);

        // Poll IR every 64 toggles (~16-32ms) to keep system responsive
        if ((i & 0x3F) == 0) {
            IR_Remote_Check_Timeout();
            if (IR_Is_New_Frame_Ready()) {
                Buzzer_Stop();
                return;
            }
        }
    }
    Buzzer_Stop();
}

void Buzzer_Beep(uint16_t freq, uint16_t duration)
{
    Buzzer_Tone(freq, duration);
}

/**
 * Non-blocking Splash Melody State Machine.
 * Plays the basic Blue Pill startup chime cleanly once at boot.
 * Keeps system 100% responsive to IR signals and auto-advances.
 */
void Splash_Melody_Process(void)
{
    static uint16_t note_idx = 0;
    static uint32_t note_start_tick = 0;
    static bool is_pausing = false;
    static uint16_t current_dur = 0;
    static bool melody_done = false;

    if (current_state != STATE_SPLASH || IR_Is_New_Frame_Ready()) {
        note_idx = 0;
        is_pausing = false;
        melody_done = false;
        Buzzer_Stop();
        return;
    }

    if (melody_done) {
        Buzzer_Stop();
        return;
    }

    uint32_t now = HAL_GetTick();
    if (note_start_tick > 0 && (now - note_start_tick) < current_dur) {
        return; // Still playing or pausing
    }

    if (!is_pausing && note_start_tick > 0) {
        // Just finished playing a note, start gap pause
        Buzzer_Stop();
        is_pausing = true;
        note_start_tick = now;
        current_dur = (note_idx < BASIC_CHIME_LENGTH) ? BASIC_BLUEPILL_CHIME[note_idx].gapMs : 25;
    } else {
        // Start next note
        if (note_start_tick > 0) {
            note_idx++;
        }
        is_pausing = false;

        if (note_idx >= BASIC_CHIME_LENGTH) {
            melody_done = true;
            Buzzer_Stop();
            return;
        }

        uint16_t freq = BASIC_BLUEPILL_CHIME[note_idx].freq;
        uint16_t dur = BASIC_BLUEPILL_CHIME[note_idx].durMs;
        current_dur = dur;
        note_start_tick = now;

        if (freq > 0) {
            Buzzer_Tone(freq, dur);
        }
    }
}

/* The DFPlayer drops commands that arrive too close together (e.g. set volume, set EQ, play). */
#define DFPLAYER_MIN_GAP_MS 50U
static uint32_t df_last_cmd_tick = 0;
static uint8_t  df_sent_any = 0;

void DFPlayer_SendCommand(uint8_t cmd, uint16_t arg)
{
    if (df_sent_any) {
        while ((HAL_GetTick() - df_last_cmd_tick) < DFPLAYER_MIN_GAP_MS) { }
    }
    uint16_t sum = 0xFF + 0x06 + cmd + 0x00 + (uint8_t)(arg >> 8) + (uint8_t)(arg & 0xFF);
    uint16_t checksum = 0 - sum;
    uint8_t packet[10] = {
        0x7E,
        0xFF,
        0x06,
        cmd,
        0x00,
        (uint8_t)(arg >> 8),
        (uint8_t)(arg & 0xFF),
        (uint8_t)(checksum >> 8),
        (uint8_t)(checksum & 0xFF),
        0xEF
    };

    /* Transmit 10-byte command frame to DFPlayer Mini on PA9 at 9600 baud */
    for (uint8_t b = 0; b < 10; b++) {
        uint8_t ch = packet[b];
        // Start bit (LOW)
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET);
        Buzzer_Delay_us(104);
        for (uint8_t i = 0; i < 8; i++) {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, (ch & (1 << i)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
            Buzzer_Delay_us(104);
        }
        // Stop bit (HIGH)
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET);
        Buzzer_Delay_us(104);
    }
    df_last_cmd_tick = HAL_GetTick();
    df_sent_any = 1;
}

/* ---- Receiving from the DFPlayer (its TX pin wired to PA1, 9600 8N1) ---------------------------------
 * Bit-banged like the transmit side: wait for the start bit, then sample every 104 us in the middle of each bit.
 * A reply frame is 10 bytes (about 10 ms), so a query blocks for that long once. */
#define DFP_RX_PIN        GPIO_PIN_1
#define DFP_RX_PORT       GPIOA
static bool (*df_rx_hook)(uint8_t *byte, uint32_t timeout_us) = 0;
static bool     df_rx_ready = false;
static bool     df_count_known = false;
static uint16_t df_track_count = 0;
static char     df_last_error[24] = "NOT ASKED YET";

void DFPlayer_SetRxHook(bool (*fn)(uint8_t *byte, uint32_t timeout_us)) { df_rx_hook = fn; }

static void DFPlayer_RxInit(void)
{
    if (df_rx_ready) return;
    GPIO_InitTypeDef gi = {0};
    gi.Pin = DFP_RX_PIN;
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_PULLUP;            /* idle high: an unconnected pin reads "no data", never garbage */
    HAL_GPIO_Init(DFP_RX_PORT, &gi);
    df_rx_ready = true;
}

static bool DFPlayer_RxByte(uint8_t *out, uint32_t timeout_us)
{
    if (df_rx_hook) return df_rx_hook(out, timeout_us);

    const uint32_t cyc_us = SystemCoreClock / 1000000UL;
    uint32_t t_wait = DWT->CYCCNT;
    while (HAL_GPIO_ReadPin(DFP_RX_PORT, DFP_RX_PIN) != GPIO_PIN_RESET) {          /* wait for the start bit */
        if ((DWT->CYCCNT - t_wait) > timeout_us * cyc_us) return false;
    }
    uint32_t t0 = DWT->CYCCNT;
    uint8_t v = 0;
    for (uint8_t i = 0; i < 8; i++) {
        uint32_t target = (156U + (uint32_t)i * 104U) * cyc_us;      /* 1.5 bit times, then every 104 us */
        while ((DWT->CYCCNT - t0) < target) { }
        if (HAL_GPIO_ReadPin(DFP_RX_PORT, DFP_RX_PIN) == GPIO_PIN_SET) v |= (uint8_t)(1U << i);
    }
    uint32_t stop = 988U * cyc_us;                                /* 9.5 bit times */
    while ((DWT->CYCCNT - t0) < stop) { }                                            /* middle of the stop bit */
    *out = v;
    return true;
}

bool DFPlayer_RefreshTrackCount(void)
{
    DFPlayer_RxInit();
    snprintf(df_last_error, sizeof(df_last_error), "NO ANSWER (PA1?)");
    df_count_known = false;

    /* Ask up to twice: the first query after power-up can be swallowed while the card is still being read. */
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        DFPlayer_SendCommand(DFP_CMD_QUERY_TF_FILES, 0);
        dfp_parser_t parser; dfp_parser_reset(&parser);
        uint32_t start = HAL_GetTick();
        while ((HAL_GetTick() - start) < 400U) {
            uint8_t b;
            if (!DFPlayer_RxByte(&b, 20000U)) continue;
            dfp_frame_t f;
            if (!dfp_parser_feed(&parser, b, &f)) continue;
            if (f.cmd == DFP_REPLY_TF_FILES) {
                df_track_count = f.param;
                df_count_known = true;
                snprintf(df_last_error, sizeof(df_last_error), "%s", f.param ? "" : "NO MP3 ON SD CARD");
                return true;
            }
            if (f.cmd == DFP_REPLY_ERROR) {
                snprintf(df_last_error, sizeof(df_last_error), "%s", f.param == 8 ? "NO SD IN DFPLAYER" : "DFPLAYER ERROR");
                break;
            }
        }
    }
    return false;
}

bool     DFPlayer_TrackCountKnown(void) { return df_count_known; }
uint16_t DFPlayer_GetTrackCount(void)   { return df_count_known ? df_track_count : 0; }
const char *DFPlayer_LastError(void)    { return df_last_error; }

void DFPlayer_NextTrack(void) { DFPlayer_SendCommand(0x01, 0); }   /* 0x01: next */
void DFPlayer_PrevTrack(void) { DFPlayer_SendCommand(0x02, 0); }   /* 0x02: previous */

void DFPlayer_PlayTrack(uint8_t trackNumber)
{
    DFPlayer_SendCommand(0x03, trackNumber); // 0x03: Specify tracking (NUM)
}

void DFPlayer_SetVolume(uint8_t vol)
{
    if (vol > 30) vol = 30;
    DFPlayer_SendCommand(0x06, vol); // 0x06: Specify volume (0-30)
}

void DFPlayer_SetEQ(uint8_t eq)
{
    if (eq > 5) eq = 0;
    DFPlayer_SendCommand(0x07, eq); // 0x07: Specify EQ (0:Normal, 1:Pop, 2:Rock, 3:Jazz, 4:Classic, 5:Base)
}

void DFPlayer_Pause(void)
{
    DFPlayer_SendCommand(0x0E, 0); // 0x0E: Pause
}

void DFPlayer_Play(void)
{
    DFPlayer_SendCommand(0x0D, 0); // 0x0D: Play
}

void Sound_PlaySelectedAlarmMelody(uint8_t melody_id)
{
    if (melody_id == BUZZER_MELODY_BASIC) {
        // Basic Blue Pill 3-tone chime (C6 -> E6 -> G6)
        for (uint8_t i = 0; i < BASIC_CHIME_LENGTH; i++) {
            if (IR_Is_New_Frame_Ready()) break;
            const SheetNote_t *bnote = &BASIC_BLUEPILL_CHIME[i];
            Buzzer_Tone(bnote->freq, bnote->durMs);
            HAL_Delay(bnote->gapMs > 0 ? bnote->gapMs : 20);
        }
    } else if (melody_id == BUZZER_MELODY_STANDARD) {
        // Standard Beep: 880Hz single tone
        Buzzer_Tone(880, 200);
    } else if (melody_id == BUZZER_MELODY_POPCORN) {
        // Popcorn Melody phrase (full melody)
        for (uint8_t i = 0; i < POPCORN_MELODY_LENGTH; i++) {
            if (IR_Is_New_Frame_Ready()) break;
            const SheetNote_t *pnote = &POPCORN_MELODY[i];
            Buzzer_Tone(pnote->freq, pnote->durMs);
            HAL_Delay(pnote->gapMs > 0 ? pnote->gapMs : 25);
        }
    } else if (melody_id == BUZZER_MELODY_WAKEUP) {
        // Wake Klaxon: piercing resonance warble
        for (uint8_t i = 0; i < WAKEUP_ALARM_LENGTH; i++) {
            if (IR_Is_New_Frame_Ready()) break;
            const MelodyNote_t *note = &WAKEUP_ALARM_MELODY[i];
            uint16_t base_dur = (1000 / (note->dur ? note->dur : 8));
            Buzzer_Tone(note->freq, base_dur);
            HAL_Delay(30);
        }
    } else if (melody_id == BUZZER_MELODY_ACTIVE_PULSE) {
        // Active Buzzer on PB6: 5 loud pulsing DC alarm bursts (180ms ON / 100ms OFF)
        for (uint8_t i = 0; i < 5; i++) {
            if (IR_Is_New_Frame_Ready()) break;
            ActiveBuzzer_On();
            HAL_Delay(180);
            ActiveBuzzer_Off();
            HAL_Delay(100);
        }
        ActiveBuzzer_Off();
    } else {
        // Sheet Music melody phrase (full melody)
        for (uint8_t i = 0; i < SHEET_MUSIC_LENGTH; i++) {
            if (IR_Is_New_Frame_Ready()) break;
            const SheetNote_t *snote = &SHEET_MUSIC_MELODY[i];
            Buzzer_Tone(snote->freq, snote->durMs);
            HAL_Delay(snote->gapMs > 0 ? snote->gapMs : 20);
        }
    }
    Buzzer_Stop();
}

void Alarm_Test_Melody_Process(void)
{
    extern uint8_t is_alarm_test_playing;

    static uint16_t test_note_idx = 0;
    static uint32_t test_note_start = 0;
    static bool test_pausing = false;
    static uint16_t test_current_dur = 0;
    static uint8_t last_melody_id = 0xFF;

    if (!is_alarm_test_playing) {
        test_note_idx = 0;
        test_pausing = false;
        test_note_start = 0;
        last_melody_id = 0xFF;
        Buzzer_Stop();
        return;
    }

    uint8_t mid = alarms[0].buzzer_melody;
    if (mid != last_melody_id) {
        test_note_idx = 0;
        test_pausing = false;
        test_note_start = 0;
        last_melody_id = mid;
    }

    uint32_t now = HAL_GetTick();
    if (test_note_start > 0 && (now - test_note_start) < test_current_dur) {
        return;
    }

    if (mid == BUZZER_MELODY_ACTIVE_PULSE) {
        if (!test_pausing) {
            ActiveBuzzer_On();
            test_current_dur = 180;
            test_pausing = true;
        } else {
            ActiveBuzzer_Off();
            test_current_dur = 100;
            test_pausing = false;
        }
        test_note_start = now;
        return;
    }

    if (!test_pausing && test_note_start > 0) {
        Buzzer_Stop();
        test_pausing = true;
        test_note_start = now;
        if (mid == BUZZER_MELODY_BASIC) {
            test_current_dur = (test_note_idx < BASIC_CHIME_LENGTH) ? BASIC_BLUEPILL_CHIME[test_note_idx].gapMs : 25;
        } else if (mid == BUZZER_MELODY_POPCORN) {
            test_current_dur = (test_note_idx < POPCORN_MELODY_LENGTH) ? POPCORN_MELODY[test_note_idx].gapMs : 25;
        } else if (mid == BUZZER_MELODY_WAKEUP) {
            test_current_dur = 30;
        } else {
            test_current_dur = (test_note_idx < SHEET_MUSIC_LENGTH) ? SHEET_MUSIC_MELODY[test_note_idx].gapMs : 20;
        }
    } else {
        if (test_note_start > 0) {
            test_note_idx++;
        }
        test_pausing = false;

        uint16_t max_len = 1;
        if (mid == BUZZER_MELODY_BASIC) max_len = BASIC_CHIME_LENGTH;
        else if (mid == BUZZER_MELODY_STANDARD) max_len = 1;
        else if (mid == BUZZER_MELODY_POPCORN) max_len = POPCORN_MELODY_LENGTH;
        else if (mid == BUZZER_MELODY_WAKEUP) max_len = WAKEUP_ALARM_LENGTH;
        else max_len = SHEET_MUSIC_LENGTH;

        if (test_note_idx >= max_len) {
            test_note_idx = 0; // Loop seamlessly while test is active
        }

        uint16_t freq = 880;
        uint16_t dur = 200;

        if (mid == BUZZER_MELODY_BASIC) {
            freq = BASIC_BLUEPILL_CHIME[test_note_idx].freq;
            dur = BASIC_BLUEPILL_CHIME[test_note_idx].durMs;
        } else if (mid == BUZZER_MELODY_STANDARD) {
            freq = 880;
            dur = 200;
        } else if (mid == BUZZER_MELODY_POPCORN) {
            freq = POPCORN_MELODY[test_note_idx].freq;
            dur = POPCORN_MELODY[test_note_idx].durMs;
        } else if (mid == BUZZER_MELODY_WAKEUP) {
            const MelodyNote_t *note = &WAKEUP_ALARM_MELODY[test_note_idx];
            freq = note->freq;
            dur = 1000 / (note->dur ? note->dur : 8);
        } else {
            freq = SHEET_MUSIC_MELODY[test_note_idx].freq;
            dur = SHEET_MUSIC_MELODY[test_note_idx].durMs;
        }

        test_current_dur = dur;
        test_note_start = now;

        if (freq > 0) {
            Buzzer_Tone(freq, dur);
        }
    }
}

void Alarm_Ringing_Process(uint32_t current_tick)
{
    extern uint8_t is_alarm_ringing;

    static uint16_t ring_note_idx = 0;
    static uint32_t ring_note_start = 0;
    static bool ring_pausing = false;
    static uint16_t ring_current_dur = 0;

    if (!is_alarm_ringing) {
        ring_note_idx = 0;
        ring_pausing = false;
        ring_note_start = 0;
        Buzzer_Stop();
        return;
    }

    uint8_t mid = alarms[0].buzzer_melody;

    uint32_t now = current_tick;
    if (ring_note_start > 0 && (now - ring_note_start) < ring_current_dur) {
        return;
    }

    if (mid == BUZZER_MELODY_ACTIVE_PULSE) {
        if (!ring_pausing) {
            ActiveBuzzer_On();
            ring_current_dur = 120;
            ring_pausing = true;
        } else {
            ActiveBuzzer_Off();
            ring_current_dur = 100;
            ring_pausing = false;
        }
        ring_note_start = now;
        return;
    }

    if (!ring_pausing && ring_note_start > 0) {
        Buzzer_Stop();
        ring_pausing = true;
        ring_note_start = now;
        if (mid == BUZZER_MELODY_BASIC) {
            ring_current_dur = (ring_note_idx < BASIC_CHIME_LENGTH) ? BASIC_BLUEPILL_CHIME[ring_note_idx].gapMs : 25;
        } else if (mid == BUZZER_MELODY_POPCORN) {
            ring_current_dur = (ring_note_idx < POPCORN_MELODY_LENGTH) ? POPCORN_MELODY[ring_note_idx].gapMs : 25;
        } else if (mid == BUZZER_MELODY_WAKEUP) {
            ring_current_dur = 25;
        } else {
            ring_current_dur = (ring_note_idx < SHEET_MUSIC_LENGTH) ? SHEET_MUSIC_MELODY[ring_note_idx].gapMs : 20;
        }
    } else {
        if (ring_note_start > 0) {
            ring_note_idx++;
        }
        ring_pausing = false;

        uint16_t max_len = 1;
        if (mid == BUZZER_MELODY_BASIC) max_len = BASIC_CHIME_LENGTH;
        else if (mid == BUZZER_MELODY_STANDARD) max_len = 1;
        else if (mid == BUZZER_MELODY_POPCORN) max_len = POPCORN_MELODY_LENGTH;
        else if (mid == BUZZER_MELODY_WAKEUP) max_len = WAKEUP_ALARM_LENGTH;
        else max_len = SHEET_MUSIC_LENGTH;

        if (ring_note_idx >= max_len) {
            ring_note_idx = 0;
        }

        uint16_t freq = 880;
        uint16_t dur = 50;

        if (mid == BUZZER_MELODY_BASIC) {
            freq = BASIC_BLUEPILL_CHIME[ring_note_idx].freq;
            dur = BASIC_BLUEPILL_CHIME[ring_note_idx].durMs;
        } else if (mid == BUZZER_MELODY_STANDARD) {
            freq = 880;
            dur = 60;
        } else if (mid == BUZZER_MELODY_POPCORN) {
            freq = POPCORN_MELODY[ring_note_idx].freq;
            dur = POPCORN_MELODY[ring_note_idx].durMs;
        } else if (mid == BUZZER_MELODY_WAKEUP) {
            const MelodyNote_t *note = &WAKEUP_ALARM_MELODY[ring_note_idx];
            freq = note->freq;
            dur = 1000 / (note->dur ? note->dur : 12);
        } else {
            freq = SHEET_MUSIC_MELODY[ring_note_idx].freq;
            dur = SHEET_MUSIC_MELODY[ring_note_idx].durMs;
        }

        if (dur > 50) dur = 50;

        ring_current_dur = dur;
        ring_note_start = now;

        if (freq > 0) {
            Buzzer_Tone(freq, dur);
        }
    }
}

static uint8_t sound_view = 0; // 0: Vol/Tone, 1: 5-Band EQ, 2: Adv. Sound Settings, 3: Stereo VU Meter, 4..9: Fine-tune
static uint8_t selected_ctrl = 0;
static uint8_t last_slider_ctrl = 0;
static uint32_t sound_timer = 0;   // render throttle (read in Sound_App_Process)
static uint8_t sound_return_state = STATE_ICON_MENU; // screen BACK returns to (set by set_system_state)

void Sound_App_SetReturnState(uint8_t return_state)
{
    sound_return_state = return_state;
}

uint8_t Sound_App_GetReturnState(void)
{
    return sound_return_state;
}

uint8_t Sound_App_GetVolume(void)
{
    return master_volume;
}

static uint8_t eq_bands[5] = {11, 11, 11, 11, 11}; // 0..21 for 31, 62, 125, 250, 500

/* DSP Advanced Sound Settings Parameters */
typedef struct {
    bool gate_enabled;
    int8_t gate_thresh_db;
    uint8_t gate_ratio;

    bool comp_enabled;
    int8_t comp_thresh_db;
    uint8_t comp_ratio;
    uint8_t comp_makeup_db;

    bool reverb_enabled;
    uint8_t reverb_preset; // 0: ROOM, 1: HALL, 2: PLATE
    uint8_t reverb_mix;

    bool wide_enabled;
    uint8_t wide_width;
    uint8_t wide_mode; // 0: HAAS, 1: M-S

    bool sat_enabled;
    uint8_t sat_drive;
    uint8_t sat_mode; // 0: TAPE, 1: TUBE

    bool cross_enabled;
    uint8_t cross_preset; // 0: BAUER, 1: CHU
    uint8_t cross_level; // 0: MEDIUM, 1: HIGH
} Dsp_Config_t;

static Dsp_Config_t dsp_cfg = {
    .gate_enabled = false,
    .gate_thresh_db = -40,
    .gate_ratio = 4,

    .comp_enabled = false,
    .comp_thresh_db = -18,
    .comp_ratio = 4,
    .comp_makeup_db = 3,

    .reverb_enabled = false,
    .reverb_preset = 0,
    .reverb_mix = 30,

    .wide_enabled = false,
    .wide_width = 130,
    .wide_mode = 0,

    .sat_enabled = false,
    .sat_drive = 6,
    .sat_mode = 0,

    .cross_enabled = false,
    .cross_preset = 0,
    .cross_level = 0
};

#define ADAU1401_I2C_ADDR ADAU_I2C_ADDR_8BIT   /* see adau_params.h */

/* Precalculated dB to 5.23 SigmaDSP fixed-point lookup table (-18dB to +18dB) */
static const int32_t DB_TO_523_TABLE[25] = {
    1054992,  // -18.0 dB (0.12589)
    1254347,  // -16.5 dB (0.14962)
    1491370,  // -15.0 dB (0.17783)
    1773187,  // -13.5 dB (0.21135)
    2108253,  // -12.0 dB (0.25119)
    2506631,  // -10.5 dB (0.29854)
    2980299,  // -9.0 dB (0.35481)
    3543465,  // -7.5 dB (0.42170)
    4213054,  // -6.0 dB (0.50119)
    5009180,  // -4.5 dB (0.59566)
    5955734,  // -3.0 dB (0.70795)
    7081156,  // -1.5 dB (0.84140)
    8388608,  //  0.0 dB (1.00000) -> 0x800000 (0dB Unity)
    9937171,  // +1.5 dB (1.18850)
    11814925, // +3.0 dB (1.41254)
    14047514, // +4.5 dB (1.67880)
    16702022, // +6.0 dB (1.99526)
    19858100, // +7.5 dB (2.37137)
    23610537, // +9.0 dB (2.81838)
    28072049, // +10.5 dB (3.34965)
    33376709, // +12.0 dB (3.98107)
    39683789, // +13.5 dB (4.73151)
    47182607, // +15.0 dB (5.62341)
    56113110, // +16.5 dB (6.68344)
    66716715  // +18.0 dB (7.94328)
};

static bool adau1401_detected = false;

bool ADAU1401_IsDetected(void)
{
    return adau1401_detected;
}

static HAL_StatusTypeDef ADAU1401_WriteParam(uint16_t param_addr, int32_t val_5_23)
{
    uint8_t data[6];
    data[0] = (uint8_t)(param_addr >> 8);
    data[1] = (uint8_t)(param_addr & 0xFF);
    data[2] = (uint8_t)((val_5_23 >> 24) & 0xFF);
    data[3] = (uint8_t)((val_5_23 >> 16) & 0xFF);
    data[4] = (uint8_t)((val_5_23 >> 8) & 0xFF);
    data[5] = (uint8_t)(val_5_23 & 0xFF);
    return HAL_I2C_Master_Transmit(&hi2c1, ADAU1401_I2C_ADDR, data, 6, 20);
}

static uint8_t last_df_vol = 0xFF, last_df_eq = 0xFF;   /* what the DFPlayer was last told */

/* dB-table index for a slider value. 'center' is the slider position that must be 0 dB. */
static uint8_t db_index(int value, int center)
{
    int idx = 12 + (value - center);   /* table index 12 = 0 dB, 1.5 dB per step */
    if (idx < 0)  idx = 0;
    if (idx > 24) idx = 24;
    return (uint8_t)idx;
}

void Sound_Apply_Hardware(void)
{
    // Is an ADAU1401/1701 answering on the I2C bus? Only talk to it if it is.
    if (!adau1401_detected && HAL_I2C_IsDeviceReady(&hi2c1, ADAU1401_I2C_ADDR, 2, 10) == HAL_OK) {
        adau1401_detected = true;
    }

    // 1. DFPlayer Mini on PA9: send volume / EQ preset only when they actually changed
    //    (each command is a ~10 ms bit-banged frame plus the mandatory gap).
    uint8_t df_vol = is_muted ? 0 : (uint8_t)((master_volume * 30) / 13);
    if (df_vol != last_df_vol) {
        DFPlayer_SetVolume(df_vol);
        last_df_vol = df_vol;
    }

    uint8_t df_eq = 0; // Normal
    if (eq_bass > 9) df_eq = 5;        // Bass boost
    else if (eq_treble > 9) df_eq = 2; // Rock / Treble boost
    else if (eq_mid > 9) df_eq = 1;    // Pop
    else if (eq_bass < 5 && eq_treble < 5) df_eq = 3; // Jazz
    if (df_eq != last_df_eq) {
        DFPlayer_SetEQ(df_eq);
        last_df_eq = df_eq;
    }

    if (!adau1401_detected) return;

    // 2. Real-time 5.23 fixed-point parameters to the ADAU over I2C1
    // Master volume 0..1.0 (0x800000)
    ADAU1401_WriteParam(ADAU_P_MASTER_VOL, is_muted ? 0 : ((int32_t)master_volume * 8388608L) / 13L);

    // Bass / mid / treble sliders are 0..13 with 7 = flat (0 dB)
    ADAU1401_WriteParam(ADAU_P_BASS,   DB_TO_523_TABLE[db_index(eq_bass, 7)]);   // 100 Hz low shelf
    ADAU1401_WriteParam(ADAU_P_MID,    DB_TO_523_TABLE[db_index(eq_mid, 7)]);    // 1 kHz peaking
    ADAU1401_WriteParam(ADAU_P_TREBLE, DB_TO_523_TABLE[db_index(eq_treble, 7)]); // 4 kHz high shelf

    // 5-band EQ sliders are 0..21 with 11 = flat (0 dB), i.e. -16.5 dB .. +15 dB in 1.5 dB steps.
    // (The table's 0 dB entry is index 12, so slider 11 must map to index 12, not 11.)
    for (uint8_t b = 0; b < 5; b++) {
        ADAU1401_WriteParam((uint16_t)(ADAU_P_EQ_BAND0 + b * ADAU_P_EQ_STRIDE),
                            DB_TO_523_TABLE[db_index(eq_bands[b], 11)]);
    }

    // Advanced sound effects
    ADAU1401_WriteParam(ADAU_P_GATE_EN,     dsp_cfg.gate_enabled ? 0x800000 : 0);
    ADAU1401_WriteParam(ADAU_P_COMP_EN,     dsp_cfg.comp_enabled ? 0x800000 : 0);
    ADAU1401_WriteParam(ADAU_P_REVERB_MIX,  ((int32_t)dsp_cfg.reverb_mix * 8388608L) / 100L);
    ADAU1401_WriteParam(ADAU_P_WIDE_WIDTH,  ((int32_t)dsp_cfg.wide_width * 8388608L) / 100L);
    ADAU1401_WriteParam(ADAU_P_SAT_DRIVE,   dsp_cfg.sat_enabled ? ((int32_t)dsp_cfg.sat_drive * 8388608L) / 20L : 0);
    ADAU1401_WriteParam(ADAU_P_CROSS_EN,    dsp_cfg.cross_enabled ? 0x800000 : 0);
}

/* Resend everything, including the DFPlayer volume/EQ (use when playback starts). */
void Sound_Apply_Hardware_Force(void)
{
    last_df_vol = 0xFF;
    last_df_eq = 0xFF;
    Sound_Apply_Hardware();
}

void Sound_App_Init(void)
{
    sound_view = 0;
    selected_ctrl = 0;
    last_slider_ctrl = 0;
    sound_timer = 0;
    /* Hardware is applied from Staplez_Process() a couple of seconds after power-up: the DFPlayer
     * is still starting and the ADAU may be self-booting from its EEPROM over this same I2C bus. */
}

/* Standard arrow buttons: every page uses the same boxes. The 16x16 arrow's pixels sit at +2..+13 across and
 * +1/+2..+14 down inside its cell, so a 28x16 box keeps every arrow pixel inside it; the selection frame is
 * drawn around it with a 2 pixel gap. */
static void nav_left(bool selected)
{
    sh1106_drawRect(2, 47, 28, 16, SH1106_WHITE);
    if (selected) sh1106_drawRect(1, 45, 30, 19, SH1106_WHITE);
    draw_16x16_icon(7, 47, sc16_leftarrow);
}

static void nav_right(bool selected)
{
    sh1106_drawRect(98, 47, 28, 16, SH1106_WHITE);
    if (selected) sh1106_drawRect(97, 45, 30, 19, SH1106_WHITE);
    draw_16x16_icon(104, 47, sc16_rightarrow);
}

void Sound_App_Render(void)
{
    sh1106_clear();

    if (sound_view == 0) {
        // PAGE 1: 4 vertical sliders at x=[19, 54, 76, 98], barY=2
        const uint8_t xCoords[4] = {19, 54, 76, 98};
        const uint8_t levels[4] = {master_volume, eq_bass, eq_mid, eq_treble};
        const char *labels[4] = {"VOL", "BAS", "MID", "TRE"};

        for (uint8_t i = 0; i < 4; i++) {
            uint8_t curX = xCoords[i];
            uint8_t lvl = levels[i];
            if (lvl > 13) lvl = 13;

            // Draw 16x32 vertical barfill
            draw_16x32_icon(curX, 2, barfill_table[lvl]);

            // Footer icon / label
            if (i == 0) {
                draw_16x16_icon(curX - 1, 33, (is_muted || master_volume == 0) ? no_sound_icon : soundon_icon);
            } else {
                sh1106_drawString(curX, 38, labels[i], 1, SH1106_WHITE);
            }

            // Selection box
            if (selected_ctrl == i) {
                sh1106_drawRect(curX - 2, 0, (i == 0 ? 20 : 21), 48, SH1106_WHITE);
            }
        }

        // NEXT button on bottom-right (Index 4): switches to Page 2 EQ
        nav_right(selected_ctrl == 4);

        // BACK button on bottom-left (Index 5): returns to main App Menu
        sh1106_drawRect(2, 50, 30, 12, SH1106_WHITE);
        if (selected_ctrl == 5) {
            sh1106_drawRect(1, 48, 33, 16, SH1106_WHITE);
        }
        sh1106_drawString(5, 52, "BACK", 1, SH1106_WHITE);

    } else if (sound_view == 1) {
        // PAGE 2: Centered 5-Band EQ (31, 62, 125, 250, 500) at x=[11, 32, 53, 74, 95]
        const uint8_t startX = 11;
        const uint8_t colWidth = 21;
        const char *eqLabels[5] = {"31", "62", "125", "250", "500"};

        for (uint8_t i = 0; i < 5; i++) {
            uint8_t curX = startX + (i * colWidth);
            uint8_t lvl = eq_bands[i];
            if (lvl > 21) lvl = 21;

            draw_16x32_icon(curX, 2, eq_slider_table[lvl]);

            uint8_t labelX = curX;
            if (i == 0) labelX += 3;
            else if (i == 1) labelX += 2;
            else labelX -= 1;

            sh1106_drawString(labelX, 35, eqLabels[i], 1, SH1106_WHITE);

            if (selected_ctrl == i) {
                uint8_t boxX = (i == 0) ? curX - 1 : ((i == 1) ? curX : curX - 2);
                uint8_t boxW = (i == 0) ? 17 : ((i == 1) ? 15 : 20);
                sh1106_drawRect(boxX, 0, boxW, 46, SH1106_WHITE);
            }
        }

        // NEXT button on bottom-right (Index 5) to Page 3 Adv. Sound Settings
        nav_right(selected_ctrl == 5);

        // PREV / BACK button on bottom-left (Index 6) back to Page 1
        nav_left(selected_ctrl == 6);

    } else if (sound_view == 2) {
        // PAGE 3: "ADV. SOUND SETTINGS" (PCM1808 ADC & DSP FX Checkbox Matrix)
        sh1106_drawString(4, 3, "ADV. SOUND SETTINGS", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 13, 128, SH1106_WHITE);

        // 6 FX Checkbox items (2 columns x 3 rows)
        // Row 0 (y=17)
        sh1106_drawString(4, 17, dsp_cfg.gate_enabled ? "[X] GATE" : "[ ] GATE", 1, SH1106_WHITE);
        sh1106_drawString(68, 17, dsp_cfg.comp_enabled ? "[X] COMP" : "[ ] COMP", 1, SH1106_WHITE);

        // Row 1 (y=27)
        sh1106_drawString(4, 27, dsp_cfg.reverb_enabled ? "[X] REV" : "[ ] REV", 1, SH1106_WHITE);
        sh1106_drawString(68, 27, dsp_cfg.wide_enabled ? "[X] WIDE" : "[ ] WIDE", 1, SH1106_WHITE);

        // Row 2 (y=37)
        sh1106_drawString(4, 37, dsp_cfg.sat_enabled ? "[X] SAT" : "[ ] SAT", 1, SH1106_WHITE);
        sh1106_drawString(68, 37, dsp_cfg.cross_enabled ? "[X] X-FEED" : "[ ] X-FEED", 1, SH1106_WHITE);

        // Selection box between [ ] of selected item
        if (selected_ctrl == 0) sh1106_drawRect(9, 16, 7, 9, SH1106_WHITE);
        else if (selected_ctrl == 1) sh1106_drawRect(73, 16, 7, 9, SH1106_WHITE);
        else if (selected_ctrl == 2) sh1106_drawRect(9, 26, 7, 9, SH1106_WHITE);
        else if (selected_ctrl == 3) sh1106_drawRect(73, 26, 7, 9, SH1106_WHITE);
        else if (selected_ctrl == 4) sh1106_drawRect(9, 36, 7, 9, SH1106_WHITE);
        else if (selected_ctrl == 5) sh1106_drawRect(73, 36, 7, 9, SH1106_WHITE);

        // NEXT button on bottom-right (Index 6)
        nav_right(selected_ctrl == 6);

        // BACK button on bottom-left (Index 7) to Page 2 5-Band EQ
        nav_left(selected_ctrl == 7);

    } else if (sound_view == 4) {
        // VIEW 4: Noise Gate Fine-Tune Menu
        sh1106_drawString(4, 2, "GATE FINE-TUNE", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 11, 128, SH1106_WHITE);

        char buf[32];
        snprintf(buf, sizeof(buf), "STATE: [%c] %s", dsp_cfg.gate_enabled ? 'X' : ' ', dsp_cfg.gate_enabled ? "ENABLED" : "DISABLED");
        sh1106_drawString(6, 14, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "THRESH: %d dB", dsp_cfg.gate_thresh_db);
        sh1106_drawString(6, 25, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "RATIO:  %u:1", dsp_cfg.gate_ratio);
        sh1106_drawString(6, 36, buf, 1, SH1106_WHITE);

        // Solitary BACK button (Index 3). NO NEXT BUTTON.
        nav_left(selected_ctrl == 3);

        if (selected_ctrl == 0) sh1106_drawRect(4, 12, 120, 11, SH1106_WHITE);
        else if (selected_ctrl == 1) sh1106_drawRect(4, 23, 120, 11, SH1106_WHITE);
        else if (selected_ctrl == 2) sh1106_drawRect(4, 34, 120, 11, SH1106_WHITE);

    } else if (sound_view == 5) {
        // VIEW 5: Dynamic Compression Fine-Tune Menu. Four rows would collide with the BACK button, so the
        // on/off box sits in the title row.
        sh1106_drawString(4, 2, "COMP FINE-TUNE", 1, SH1106_WHITE);
        sh1106_drawString(104, 2, dsp_cfg.comp_enabled ? "[X]" : "[ ]", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 11, 128, SH1106_WHITE);
        if (selected_ctrl == 0) sh1106_drawRect(101, 0, 24, 11, SH1106_WHITE);

        char buf[32];
        snprintf(buf, sizeof(buf), "THRESH: %d dB", dsp_cfg.comp_thresh_db);
        sh1106_drawString(6, 14, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "RATIO:  %u:1", dsp_cfg.comp_ratio);
        sh1106_drawString(6, 24, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "MAKEUP: +%u dB", dsp_cfg.comp_makeup_db);
        sh1106_drawString(6, 34, buf, 1, SH1106_WHITE);

        if (selected_ctrl == 1) sh1106_drawRect(4, 12, 120, 10, SH1106_WHITE);
        else if (selected_ctrl == 2) sh1106_drawRect(4, 22, 120, 10, SH1106_WHITE);
        else if (selected_ctrl == 3) sh1106_drawRect(4, 32, 120, 10, SH1106_WHITE);
        nav_left(selected_ctrl == 4);

    } else if (sound_view == 6) {
        // VIEW 6: Reverb FX Fine-Tune Menu
        sh1106_drawString(4, 2, "REVERB FINE-TUNE", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 11, 128, SH1106_WHITE);

        const char *presetNames[3] = {"ROOM", "HALL", "PLATE"};
        char buf[32];
        snprintf(buf, sizeof(buf), "STATE: [%c] %s", dsp_cfg.reverb_enabled ? 'X' : ' ', dsp_cfg.reverb_enabled ? "ENABLED" : "DISABLED");
        sh1106_drawString(6, 14, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "PRESET: %s", presetNames[dsp_cfg.reverb_preset % 3]);
        sh1106_drawString(6, 25, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "WET MIX:%u%%", dsp_cfg.reverb_mix);
        sh1106_drawString(6, 36, buf, 1, SH1106_WHITE);

        // Solitary BACK button (Index 3). NO NEXT BUTTON.
        nav_left(selected_ctrl == 3);

        if (selected_ctrl == 0) sh1106_drawRect(4, 12, 120, 11, SH1106_WHITE);
        else if (selected_ctrl == 1) sh1106_drawRect(4, 23, 120, 11, SH1106_WHITE);
        else if (selected_ctrl == 2) sh1106_drawRect(4, 34, 120, 11, SH1106_WHITE);

    } else if (sound_view == 7) {
        // VIEW 7: Stereo Widening Fine-Tune Menu
        sh1106_drawString(4, 2, "WIDE FINE-TUNE", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 11, 128, SH1106_WHITE);

        const char *modeNames[2] = {"HAAS", "M-S"};
        char buf[32];
        snprintf(buf, sizeof(buf), "STATE: [%c] %s", dsp_cfg.wide_enabled ? 'X' : ' ', dsp_cfg.wide_enabled ? "ENABLED" : "DISABLED");
        sh1106_drawString(6, 14, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "WIDTH:  %u%%", dsp_cfg.wide_width);
        sh1106_drawString(6, 25, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "MODE:   %s", modeNames[dsp_cfg.wide_mode % 2]);
        sh1106_drawString(6, 36, buf, 1, SH1106_WHITE);

        // Solitary BACK button (Index 3). NO NEXT BUTTON.
        nav_left(selected_ctrl == 3);

        if (selected_ctrl == 0) sh1106_drawRect(4, 12, 120, 11, SH1106_WHITE);
        else if (selected_ctrl == 1) sh1106_drawRect(4, 23, 120, 11, SH1106_WHITE);
        else if (selected_ctrl == 2) sh1106_drawRect(4, 34, 120, 11, SH1106_WHITE);

    } else if (sound_view == 8) {
        // VIEW 8: Harmonic Saturation Fine-Tune Menu
        sh1106_drawString(4, 2, "SAT FINE-TUNE", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 11, 128, SH1106_WHITE);

        const char *satModes[2] = {"TAPE", "TUBE"};
        char buf[32];
        snprintf(buf, sizeof(buf), "STATE: [%c] %s", dsp_cfg.sat_enabled ? 'X' : ' ', dsp_cfg.sat_enabled ? "ENABLED" : "DISABLED");
        sh1106_drawString(6, 14, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "DRIVE:  +%u%%", dsp_cfg.sat_drive);
        sh1106_drawString(6, 25, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "COLOR:  %s", satModes[dsp_cfg.sat_mode % 2]);
        sh1106_drawString(6, 36, buf, 1, SH1106_WHITE);

        // Solitary BACK button (Index 3). NO NEXT BUTTON.
        nav_left(selected_ctrl == 3);

        if (selected_ctrl == 0) sh1106_drawRect(4, 12, 120, 11, SH1106_WHITE);
        else if (selected_ctrl == 1) sh1106_drawRect(4, 23, 120, 11, SH1106_WHITE);
        else if (selected_ctrl == 2) sh1106_drawRect(4, 34, 120, 11, SH1106_WHITE);

    } else if (sound_view == 9) {
        // VIEW 9: Headphone Crossfeed Fine-Tune Menu
        sh1106_drawString(4, 2, "X-FEED FINE-TUNE", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 11, 128, SH1106_WHITE);

        const char *crossTypes[2] = {"BAUER", "CHU"};
        const char *crossLevels[2] = {"MEDIUM", "HIGH"};
        char buf[32];
        snprintf(buf, sizeof(buf), "STATE: [%c] %s", dsp_cfg.cross_enabled ? 'X' : ' ', dsp_cfg.cross_enabled ? "ENABLED" : "DISABLED");
        sh1106_drawString(6, 14, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "TYPE:   %s", crossTypes[dsp_cfg.cross_preset % 2]);
        sh1106_drawString(6, 25, buf, 1, SH1106_WHITE);

        snprintf(buf, sizeof(buf), "LEVEL:  %s", crossLevels[dsp_cfg.cross_level % 2]);
        sh1106_drawString(6, 36, buf, 1, SH1106_WHITE);

        // Solitary BACK button (Index 3). NO NEXT BUTTON.
        nav_left(selected_ctrl == 3);

        if (selected_ctrl == 0) sh1106_drawRect(4, 12, 120, 11, SH1106_WHITE);
        else if (selected_ctrl == 1) sh1106_drawRect(4, 23, 120, 11, SH1106_WHITE);
        else if (selected_ctrl == 2) sh1106_drawRect(4, 34, 120, 11, SH1106_WHITE);

    } else {
        // VIEW 3: PCM1808 Stereo VU Meter & Level Display (Page 4)
        sh1106_drawString(4, 2, "STEREO VU METER", 1, SH1106_WHITE);
        sh1106_drawString(104, 2, "96k", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 10, 128, SH1106_WHITE);

        // Stereo Channels Left & Right peak bars
        sh1106_drawString(4, 15, "CH 1 (L):", 1, SH1106_WHITE);
        sh1106_drawRect(60, 16, 64, 10, SH1106_WHITE);
        uint32_t tick = HAL_GetTick();
        uint8_t lWidth = (uint8_t)(((tick / 50) % 24) * 2 + 10);
        if (lWidth > 60) lWidth = 60;
        sh1106_fillRect(62, 18, lWidth, 6, SH1106_WHITE);

        sh1106_drawString(4, 30, "CH 2 (R):", 1, SH1106_WHITE);
        sh1106_drawRect(60, 31, 64, 10, SH1106_WHITE);
        uint8_t rWidth = (uint8_t)((((tick + 200) / 45) % 22) * 2 + 12);
        if (rWidth > 60) rWidth = 60;
        sh1106_fillRect(62, 33, rWidth, 6, SH1106_WHITE);

        nav_left(selected_ctrl == 0);     // Index 0: back to Page 3 Adv. Sound Settings
        nav_right(selected_ctrl == 1);    // Index 1: loops to Page 1 Volume & Tone
    }

    sh1106_update();
}

void Sound_App_Process(char key, uint32_t current_tick)
{
    if (key != 0) {
        uint8_t audio_param_changed = 0;

        if (key == '4') {
            // IR LEFT (0x72C8)
            if (sound_view == 2) {
                // Page 3 Adv. Sound Settings Left/Right swaps
                if (selected_ctrl == 0) selected_ctrl = 1;      // GATE -> COMP
                else if (selected_ctrl == 1) selected_ctrl = 0; // COMP -> GATE
                else if (selected_ctrl == 2) selected_ctrl = 3; // REV -> WIDE
                else if (selected_ctrl == 3) selected_ctrl = 2; // WIDE -> REV
                else if (selected_ctrl == 4) selected_ctrl = 5; // SAT -> X-FEED
                else if (selected_ctrl == 5) selected_ctrl = 4; // X-FEED -> SAT
                else if (selected_ctrl == 6) selected_ctrl = 7; // NEXT -> BACK
                else if (selected_ctrl == 7) selected_ctrl = 6; // BACK -> NEXT
            } else if (sound_view >= 4 && sound_view <= 9) {
                // Fine-tune parameter decrease
                if (sound_view == 4) { // GATE
                    if (selected_ctrl == 1 && dsp_cfg.gate_thresh_db > -60) { dsp_cfg.gate_thresh_db -= 2; audio_param_changed = 1; }
                    else if (selected_ctrl == 2 && dsp_cfg.gate_ratio > 1) { dsp_cfg.gate_ratio--; audio_param_changed = 1; }
                } else if (sound_view == 5) { // COMP
                    if (selected_ctrl == 1 && dsp_cfg.comp_thresh_db > -40) { dsp_cfg.comp_thresh_db -= 2; audio_param_changed = 1; }
                    else if (selected_ctrl == 2 && dsp_cfg.comp_ratio > 1) { dsp_cfg.comp_ratio--; audio_param_changed = 1; }
                    else if (selected_ctrl == 3 && dsp_cfg.comp_makeup_db > 0) { dsp_cfg.comp_makeup_db--; audio_param_changed = 1; }
                } else if (sound_view == 6) { // REV
                    if (selected_ctrl == 1 && dsp_cfg.reverb_preset > 0) { dsp_cfg.reverb_preset--; audio_param_changed = 1; }
                    else if (selected_ctrl == 2 && dsp_cfg.reverb_mix >= 5) { dsp_cfg.reverb_mix -= 5; audio_param_changed = 1; }
                } else if (sound_view == 7) { // WIDE
                    if (selected_ctrl == 1 && dsp_cfg.wide_width > 100) { dsp_cfg.wide_width -= 5; audio_param_changed = 1; }
                    else if (selected_ctrl == 2) { dsp_cfg.wide_mode = !dsp_cfg.wide_mode; audio_param_changed = 1; }
                } else if (sound_view == 8) { // SAT
                    if (selected_ctrl == 1 && dsp_cfg.sat_drive > 0) { dsp_cfg.sat_drive--; audio_param_changed = 1; }
                    else if (selected_ctrl == 2) { dsp_cfg.sat_mode = !dsp_cfg.sat_mode; audio_param_changed = 1; }
                } else if (sound_view == 9) { // X-FEED
                    if (selected_ctrl == 1) { dsp_cfg.cross_preset = !dsp_cfg.cross_preset; audio_param_changed = 1; }
                    else if (selected_ctrl == 2) { dsp_cfg.cross_level = !dsp_cfg.cross_level; audio_param_changed = 1; }
                }
            } else if (sound_view == 3) {
                // Page 4 Stereo VU Meter Left/Right
                selected_ctrl = (selected_ctrl == 0) ? 1 : 0;
            } else {
                uint8_t max_idx = (sound_view == 0) ? 5 : 6;
                selected_ctrl = (selected_ctrl == 0) ? max_idx : selected_ctrl - 1;
                if (selected_ctrl < 4) last_slider_ctrl = selected_ctrl;
            }

        } else if (key == '6') {
            // IR RIGHT (0x72D8)
            if (sound_view == 2) {
                // Page 3 Adv. Sound Settings Left/Right swaps
                if (selected_ctrl == 0) selected_ctrl = 1;      // GATE -> COMP
                else if (selected_ctrl == 1) selected_ctrl = 0; // COMP -> GATE
                else if (selected_ctrl == 2) selected_ctrl = 3; // REV -> WIDE
                else if (selected_ctrl == 3) selected_ctrl = 2; // WIDE -> REV
                else if (selected_ctrl == 4) selected_ctrl = 5; // SAT -> X-FEED
                else if (selected_ctrl == 5) selected_ctrl = 4; // X-FEED -> SAT
                else if (selected_ctrl == 6) selected_ctrl = 7; // NEXT -> BACK
                else if (selected_ctrl == 7) selected_ctrl = 6; // BACK -> NEXT
            } else if (sound_view >= 4 && sound_view <= 9) {
                // Fine-tune parameter increase
                if (sound_view == 4) { // GATE
                    if (selected_ctrl == 1 && dsp_cfg.gate_thresh_db < 0) { dsp_cfg.gate_thresh_db += 2; audio_param_changed = 1; }
                    else if (selected_ctrl == 2 && dsp_cfg.gate_ratio < 10) { dsp_cfg.gate_ratio++; audio_param_changed = 1; }
                } else if (sound_view == 5) { // COMP
                    if (selected_ctrl == 1 && dsp_cfg.comp_thresh_db < 0) { dsp_cfg.comp_thresh_db += 2; audio_param_changed = 1; }
                    else if (selected_ctrl == 2 && dsp_cfg.comp_ratio < 12) { dsp_cfg.comp_ratio++; audio_param_changed = 1; }
                    else if (selected_ctrl == 3 && dsp_cfg.comp_makeup_db < 12) { dsp_cfg.comp_makeup_db++; audio_param_changed = 1; }
                } else if (sound_view == 6) { // REV
                    if (selected_ctrl == 1 && dsp_cfg.reverb_preset < 2) { dsp_cfg.reverb_preset++; audio_param_changed = 1; }
                    else if (selected_ctrl == 2 && dsp_cfg.reverb_mix <= 95) { dsp_cfg.reverb_mix += 5; audio_param_changed = 1; }
                } else if (sound_view == 7) { // WIDE
                    if (selected_ctrl == 1 && dsp_cfg.wide_width < 200) { dsp_cfg.wide_width += 5; audio_param_changed = 1; }
                    else if (selected_ctrl == 2) { dsp_cfg.wide_mode = !dsp_cfg.wide_mode; audio_param_changed = 1; }
                } else if (sound_view == 8) { // SAT
                    if (selected_ctrl == 1 && dsp_cfg.sat_drive < 20) { dsp_cfg.sat_drive++; audio_param_changed = 1; }
                    else if (selected_ctrl == 2) { dsp_cfg.sat_mode = !dsp_cfg.sat_mode; audio_param_changed = 1; }
                } else if (sound_view == 9) { // X-FEED
                    if (selected_ctrl == 1) { dsp_cfg.cross_preset = !dsp_cfg.cross_preset; audio_param_changed = 1; }
                    else if (selected_ctrl == 2) { dsp_cfg.cross_level = !dsp_cfg.cross_level; audio_param_changed = 1; }
                }
            } else if (sound_view == 3) {
                selected_ctrl = (selected_ctrl + 1) % 2;
            } else {
                uint8_t count = (sound_view == 0) ? 6 : 7;
                selected_ctrl = (selected_ctrl + 1) % count;
                if (selected_ctrl < 4) last_slider_ctrl = selected_ctrl;
            }

        } else if (key == '2') {
            // IR UP (0xF2F0)
            if (sound_view == 0) {
                if (selected_ctrl == 0 && master_volume < 13) { master_volume++; audio_param_changed = 1; }
                else if (selected_ctrl == 1 && eq_bass < 13) { eq_bass++; audio_param_changed = 1; }
                else if (selected_ctrl == 2 && eq_mid < 13) { eq_mid++; audio_param_changed = 1; }
                else if (selected_ctrl == 3 && eq_treble < 13) { eq_treble++; audio_param_changed = 1; }
            } else if (sound_view == 1) {
                if (selected_ctrl < 5 && eq_bands[selected_ctrl] < 21) {
                    eq_bands[selected_ctrl]++;
                    audio_param_changed = 1;
                }
            } else if (sound_view == 2) {
                // Page 3 Adv. Sound Settings UP Navigation rules
                if (selected_ctrl == 6) selected_ctrl = 5;      // NEXT -> X-FEED
                else if (selected_ctrl == 1) selected_ctrl = 4; // COMP -> SAT
                else if (selected_ctrl == 0) selected_ctrl = 4; // GATE -> SAT
                else if (selected_ctrl == 2) selected_ctrl = 0; // REV -> GATE
                else if (selected_ctrl == 3) selected_ctrl = 1; // WIDE -> COMP
                else if (selected_ctrl == 4) selected_ctrl = 2; // SAT -> REV
                else if (selected_ctrl == 5) selected_ctrl = 3; // X-FEED -> WIDE
                else if (selected_ctrl == 7) selected_ctrl = 4; // BACK -> SAT
            } else if (sound_view >= 4 && sound_view <= 9) {
                uint8_t max_items = (sound_view == 5) ? 5 : 4;
                selected_ctrl = (selected_ctrl == 0) ? max_items - 1 : selected_ctrl - 1;
            }

        } else if (key == '8') {
            // IR DOWN (0xF2E0)
            if (sound_view == 0) {
                if (selected_ctrl == 0 && master_volume > 0) { master_volume--; audio_param_changed = 1; }
                else if (selected_ctrl == 1 && eq_bass > 0) { eq_bass--; audio_param_changed = 1; }
                else if (selected_ctrl == 2 && eq_mid > 0) { eq_mid--; audio_param_changed = 1; }
                else if (selected_ctrl == 3 && eq_treble > 0) { eq_treble--; audio_param_changed = 1; }
            } else if (sound_view == 1) {
                if (selected_ctrl < 5 && eq_bands[selected_ctrl] > 0) {
                    eq_bands[selected_ctrl]--;
                    audio_param_changed = 1;
                }
            } else if (sound_view == 2) {
                // Page 3 Adv. Sound Settings DOWN Navigation rules
                if (selected_ctrl == 5) selected_ctrl = 6;      // X-FEED -> NEXT
                else if (selected_ctrl == 4) selected_ctrl = 1; // SAT -> COMP
                else if (selected_ctrl == 0) selected_ctrl = 2; // GATE -> REV
                else if (selected_ctrl == 1) selected_ctrl = 3; // COMP -> WIDE
                else if (selected_ctrl == 2) selected_ctrl = 4; // REV -> SAT
                else if (selected_ctrl == 3) selected_ctrl = 5; // WIDE -> X-FEED
                else if (selected_ctrl == 6) selected_ctrl = 0; // NEXT -> GATE
                else if (selected_ctrl == 7) selected_ctrl = 0; // BACK -> GATE
            } else if (sound_view >= 4 && sound_view <= 9) {
                uint8_t max_items = (sound_view == 5) ? 5 : 4;
                selected_ctrl = (selected_ctrl + 1) % max_items;
            }

        } else if (key == '#') {
            // IR PLAY (0x7158)
            if (sound_view == 0) {
                if (selected_ctrl == 0) { is_muted = !is_muted; audio_param_changed = 1; }
                else if (selected_ctrl == 4) { sound_view = 1; selected_ctrl = 0; } // NEXT -> Page 2 EQ
                else if (selected_ctrl == 5) { set_system_state((SystemState_t)sound_return_state); return; } // BACK -> Menu or MP3 Player
            } else if (sound_view == 1) {
                if (selected_ctrl == 5) { sound_view = 2; selected_ctrl = 0; } // NEXT -> Page 3 Adv. Sound Settings
                else if (selected_ctrl == 6) { sound_view = 0; selected_ctrl = 4; } // BACK -> Page 1
            } else if (sound_view == 2) {
                if (selected_ctrl <= 5) {   /* unsigned: '>= 0' was always true */
                    // Open Fine-Tune Submenu
                    sound_view = 4 + selected_ctrl;
                    selected_ctrl = 0;
                } else if (selected_ctrl == 6) {
                    // NEXT -> Page 4 (Stereo VU Meter)
                    sound_view = 3;
                    selected_ctrl = 0;
                } else if (selected_ctrl == 7) {
                    // BACK -> Page 2 (5-Band EQ)
                    sound_view = 1;
                    selected_ctrl = 5;
                }
            } else if (sound_view >= 4 && sound_view <= 9) {
                uint8_t back_idx = (sound_view == 5) ? 4 : 3;
                if (selected_ctrl == back_idx) {
                    // Solitary BACK button in fine-tune submenu: return to Adv. Sound Settings
                    uint8_t return_fx = sound_view - 4;
                    sound_view = 2;
                    selected_ctrl = return_fx;
                } else if (selected_ctrl == 0) {
                    // Toggle enable state
                    if (sound_view == 4) dsp_cfg.gate_enabled = !dsp_cfg.gate_enabled;
                    else if (sound_view == 5) dsp_cfg.comp_enabled = !dsp_cfg.comp_enabled;
                    else if (sound_view == 6) dsp_cfg.reverb_enabled = !dsp_cfg.reverb_enabled;
                    else if (sound_view == 7) dsp_cfg.wide_enabled = !dsp_cfg.wide_enabled;
                    else if (sound_view == 8) dsp_cfg.sat_enabled = !dsp_cfg.sat_enabled;
                    else if (sound_view == 9) dsp_cfg.cross_enabled = !dsp_cfg.cross_enabled;
                    audio_param_changed = 1;
                }
            } else if (sound_view == 3) {
                // Page 4 Stereo VU Meter
                if (selected_ctrl == 0) {
                    // BACK -> Page 3 Adv. Sound Settings
                    sound_view = 2;
                    selected_ctrl = 6;
                } else if (selected_ctrl == 1) {
                    // NEXT / LOOP -> Page 1 Volume & Tone
                    sound_view = 0;
                    selected_ctrl = 0;
                }
            }

        } else if (key == '*') {
            // IR SHIFT / BACK (0x7198)
            if (sound_view >= 4 && sound_view <= 9) {
                uint8_t return_fx = sound_view - 4;
                sound_view = 2;
                selected_ctrl = return_fx;
            } else if (sound_view == 0) {
                if (selected_ctrl == 5) {
                    set_system_state((SystemState_t)sound_return_state);
                    return;
                } else {
                    selected_ctrl = 5; // Focus BACK
                }
            } else if (sound_view == 1) {
                selected_ctrl = 6; // Focus BACK
            } else if (sound_view == 2) {
                selected_ctrl = 7; // Focus BACK
            } else if (sound_view == 3) {
                selected_ctrl = 0; // Focus BACK
            }
        }

        if (audio_param_changed) {
            Sound_Apply_Hardware();
        }

        sound_timer = current_tick;
        Sound_App_Render();
        return;
    }

    if ((current_tick - sound_timer) >= 50) {
        sound_timer = current_tick;
        Sound_App_Render();
    }
}
