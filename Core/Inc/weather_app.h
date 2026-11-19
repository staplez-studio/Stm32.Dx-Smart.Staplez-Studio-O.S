/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    weather_app.h
  * @brief   Weather Application Forecast Viewer Header.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __WEATHER_APP_H__
#define __WEATHER_APP_H__

#include <stdint.h>

void Weather_App_Process(char key, uint32_t current_tick);
void Weather_Background(uint32_t current_tick);
#include <stdbool.h>
#include "wifi_net.h"
/* Current weather for the main clock icon. True only while online with a reading from the last 3 hours;
 * otherwise the clock shows the plain sun / moon for the time of day. */
bool Weather_GetCurrent(wnet_icon_t *icon, int16_t *temp);
void Weather_DrawIcon16(uint8_t x, uint8_t y, wnet_icon_t icon);   /* called by the Wi-Fi manager while online */

#endif
