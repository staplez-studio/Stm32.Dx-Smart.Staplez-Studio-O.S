/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    weather_icons.h
  * @brief   True Bitmap declarations matching Staplez Studio OS Emulator to a tee.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __WEATHER_ICONS_H__
#define __WEATHER_ICONS_H__

#include <stdint.h>

/* Composite 32x16 Sunny & Moon Day/Night Icons */
extern const uint16_t weather_sunny_left[16];
extern const uint16_t weather_sunny_right[16];
extern const uint16_t weather_moon_left[16];
extern const uint16_t weather_moon_right[16];

/* Composite 32x16 Battery Icon */
extern const uint16_t noBat_left[16];
extern const uint16_t noBat_right[16];

/* Standard 16x16 App Icons */
extern const uint16_t sc16_wifi_signal[16];
extern const uint16_t sc16_clock[16];
extern const uint16_t sc16_music_note[16];
extern const uint16_t sc16_double_music_note[16];
extern const uint16_t Fan_contrl[16];
extern const uint16_t soundon_icon[16];
extern const uint16_t no_sound_icon[16];
extern const uint16_t sc16_gear[16];
extern const uint16_t game_icon[16];
extern const uint16_t sc16_leftarrow[16];
extern const uint16_t sc16_harddrive[16];
extern const uint16_t sc16_rightarrow[16];

/* Entertainment Sub-App Icons */
extern const uint16_t folder_icon[16];
extern const uint16_t mp3_player_icon[16];
extern const uint16_t sc16_screen_calibrator[16];
extern const uint16_t sc16_rfid_card[16];
extern const uint16_t weather_sunny_16[16];

/* 8x8 Status & Arrow Bitmaps */
extern const uint8_t icon_check_8x8[8];
extern const uint8_t icon_cross_8x8[8];
extern const uint8_t icon_uparrow_8x8[8];
extern const uint8_t icon_downarrow_8x8[8];

/* 16x32 Equalizer & Volume Vertical Bar Tables */
extern const uint16_t barfill_table[14][32];
extern const uint16_t eq_slider_table[22][32];

/* 128x64 Splash Screen Bitmap */
extern const uint8_t pixel_art_grid_1792549062222[1024];

#endif /* __WEATHER_ICONS_H__ */
