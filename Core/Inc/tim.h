/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.h
  * @brief   Timer configuration header for TIM1 PWM fan speed control (PA8).
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __TIM_H__
#define __TIM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern TIM_HandleTypeDef htim1;

void MX_TIM1_Init(void);
void Fan_Set_PWM(uint8_t duty_percent);

#ifdef __cplusplus
}
#endif
#endif /*__ TIM_H__ */
