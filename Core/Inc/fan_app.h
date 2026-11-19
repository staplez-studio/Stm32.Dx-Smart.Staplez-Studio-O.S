/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    fan_app.h
  * @brief   Fan Controller Application header.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __FAN_APP_H__
#define __FAN_APP_H__

#include <stdint.h>
#include <stdbool.h>

void Fan_App_Init(void);
void Fan_App_Process(char key, uint32_t current_tick);
void Fan_App_Render(void);
bool Fan_Is_Running(void);

#endif
