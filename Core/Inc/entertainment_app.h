/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    entertainment_app.h
  * @brief   Entertainment Suite (File Explorer, MP3 Player, Screen Calibrator).
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __ENTERTAINMENT_APP_H__
#define __ENTERTAINMENT_APP_H__

#include <stdint.h>

void Entertainment_App_Init(void);
void Entertainment_App_Process(char key, uint32_t current_tick);

#endif
