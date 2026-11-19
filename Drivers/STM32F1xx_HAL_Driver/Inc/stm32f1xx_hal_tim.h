/**
  ******************************************************************************
  * @file    stm32f1xx_hal_tim.h
  * @brief   Header file of TIM HAL module.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_TIM_H
#define __STM32F1xx_HAL_TIM_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"

#define TIM_CHANNEL_1                      0x00000000U
#define TIM_CHANNEL_2                      0x00000004U
#define TIM_CHANNEL_3                      0x00000008U
#define TIM_CHANNEL_4                      0x0000000CU

#define TIM_OCMODE_TIMING                  0x00000000U
#define TIM_OCMODE_PWM1                    0x00000060U
#define TIM_OCPOLARITY_HIGH                0x00000000U
#define TIM_OCFAST_DISABLE                 0x00000000U

#define TIM_COUNTERMODE_UP                 0x00000000U
#define TIM_CLOCKDIVISION_DIV1             0x00000000U
#define TIM_AUTORELOAD_PRELOAD_ENABLE      0x00000080U


typedef struct {
  uint32_t Prescaler;
  uint32_t CounterMode;
  uint32_t Period;
  uint32_t ClockDivision;
  uint32_t RepetitionCounter;
  uint32_t AutoReloadPreload;
} TIM_Base_InitTypeDef;

typedef struct {
  uint32_t OCMode;
  uint32_t Pulse;
  uint32_t OCPolarity;
  uint32_t OCFastMode;
} TIM_OC_InitTypeDef;

typedef struct {
  TIM_TypeDef                        *Instance;
  TIM_Base_InitTypeDef               Init;
  HAL_LockTypeDef                    Lock;
  __IO uint32_t                      State;
} TIM_HandleTypeDef;

HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *htim, TIM_OC_InitTypeDef *sConfig, uint32_t Channel);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t Channel);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *htim, uint32_t Channel);

#define __HAL_TIM_SET_COMPARE(__HANDLE__, __CHANNEL__, __COMPARE__)   (((__CHANNEL__) == TIM_CHANNEL_1) ? ((__HANDLE__)->Instance->CCR1 = (__COMPARE__)) :   ((__CHANNEL__) == TIM_CHANNEL_2) ? ((__HANDLE__)->Instance->CCR2 = (__COMPARE__)) :   ((__CHANNEL__) == TIM_CHANNEL_3) ? ((__HANDLE__)->Instance->CCR3 = (__COMPARE__)) :   ((__HANDLE__)->Instance->CCR4 = (__COMPARE__)))

#define __HAL_TIM_GET_COMPARE(__HANDLE__, __CHANNEL__)   (((__CHANNEL__) == TIM_CHANNEL_1) ? ((__HANDLE__)->Instance->CCR1) :   ((__CHANNEL__) == TIM_CHANNEL_2) ? ((__HANDLE__)->Instance->CCR2) :   ((__CHANNEL__) == TIM_CHANNEL_3) ? ((__HANDLE__)->Instance->CCR3) :   ((__HANDLE__)->Instance->CCR4))

#define __HAL_TIM_SET_AUTORELOAD(__HANDLE__, __AUTORELOAD__)   ((__HANDLE__)->Instance->ARR = (__AUTORELOAD__))

#define __HAL_TIM_SET_PRESCALER(__HANDLE__, __PRESC__)   ((__HANDLE__)->Instance->PSC = (__PRESC__))

#include "stm32f1xx_hal_tim_ex.h"

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_HAL_TIM_H */
