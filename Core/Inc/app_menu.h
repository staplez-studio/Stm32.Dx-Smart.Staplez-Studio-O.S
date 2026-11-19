/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    app_menu.h
  * @brief   7-App Carousel sliding window menu header.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __APP_MENU_H__
#define __APP_MENU_H__

#include <stdint.h>

void App_Menu_Init(void);
void App_Menu_Render(void);
void App_Menu_Process(char key, uint32_t current_tick);
void App_Menu_IR_Process(uint32_t ir_code);

#endif
