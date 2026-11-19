/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    main_screen.h
  * @brief   Main Real-Time Clock home screen header.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __MAIN_SCREEN_H__
#define __MAIN_SCREEN_H__

#include <stdint.h>

void Main_Screen_Init(void);
void Main_Screen_Process(char key, uint32_t current_tick);
void Main_Screen_Render(void);
uint8_t Main_Screen_Is_Gear_Selected(void);
void Main_Screen_Set_Gear_Selected(uint8_t sel);

#endif
