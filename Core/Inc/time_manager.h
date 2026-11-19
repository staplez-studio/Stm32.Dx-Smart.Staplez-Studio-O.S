/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    time_manager.h
  * @brief   Real-Time Clock setting application (Hour, Min, AM/PM, Date).
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __TIME_MANAGER_H__
#define __TIME_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>

extern bool time_is_set;

void Time_Manager_Enter(void);
void Time_Manager_Process(char key, uint32_t current_tick);

#endif
