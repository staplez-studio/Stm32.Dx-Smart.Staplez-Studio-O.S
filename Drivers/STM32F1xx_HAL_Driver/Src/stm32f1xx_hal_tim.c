/**
  ******************************************************************************
  * @file    stm32f1xx_hal_tim.c
  * @brief   TIM HAL module driver.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"

HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *htim)
{
  htim->Instance->PSC = htim->Init.Prescaler;
  htim->Instance->ARR = htim->Init.Period;
  htim->Instance->CR1 |= 0x0001; // Enable timer counter
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *htim)
{
  return HAL_TIM_Base_Init(htim);
}

HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *htim, TIM_OC_InitTypeDef *sConfig, uint32_t Channel)
{
  (void)Channel;
  htim->Instance->CCMR1 |= 0x0068; // PWM mode 1, preload enable
  htim->Instance->CCER |= 0x0001;  // CC1E enable
  htim->Instance->CCR1 = sConfig->Pulse;
  htim->Instance->BDTR |= 0x8000;  // MOE main output enable (for TIM1)
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t Channel)
{
  (void)Channel;
  htim->Instance->CCER |= 0x0001;
  htim->Instance->CR1 |= 0x0001;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *htim, uint32_t Channel)
{
  (void)Channel;
  htim->Instance->CCER &= ~0x0001;
  return HAL_OK;
}
