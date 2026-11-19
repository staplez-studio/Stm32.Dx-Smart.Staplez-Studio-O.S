/**
  ******************************************************************************
  * @file    stm32f1xx_hal_tim_ex.c
  * @brief   TIM EX HAL module driver.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_tim_ex.h"

HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *htim, TIM_MasterConfigTypeDef *sMasterConfig)
{
  (void)htim; (void)sMasterConfig;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIMEx_ConfigBreakDeadTime(TIM_HandleTypeDef *htim, TIM_BreakDeadTimeConfigTypeDef *sBreakDeadTimeConfig)
{
  (void)htim; (void)sBreakDeadTimeConfig;
  return HAL_OK;
}
