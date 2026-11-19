/**
  ******************************************************************************
  * @file    stm32f1xx_hal_tim_ex.h
  * @brief   Header file of TIM EX HAL module.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_TIM_EX_H
#define __STM32F1xx_HAL_TIM_EX_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"

typedef struct {
  uint32_t OffStateRunMode;
  uint32_t OffStateIDLEMode;
  uint32_t LockLevel;
  uint32_t DeadTime;
  uint32_t BreakState;
  uint32_t BreakPolarity;
  uint32_t AutomaticOutput;
} TIM_BreakDeadTimeConfigTypeDef;

typedef struct {
  uint32_t MasterOutputTrigger;
  uint32_t MasterSlaveMode;
} TIM_MasterConfigTypeDef;

HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *htim, TIM_MasterConfigTypeDef *sMasterConfig);
HAL_StatusTypeDef HAL_TIMEx_ConfigBreakDeadTime(TIM_HandleTypeDef *htim, TIM_BreakDeadTimeConfigTypeDef *sBreakDeadTimeConfig);

#ifdef __cplusplus
}
#endif

#endif
