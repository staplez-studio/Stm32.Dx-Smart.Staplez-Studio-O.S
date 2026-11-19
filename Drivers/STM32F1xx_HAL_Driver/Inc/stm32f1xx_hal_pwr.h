/**
  ******************************************************************************
  * @file    stm32f1xx_hal_pwr.h
  * @brief   Header file of PWR HAL module.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_PWR_H
#define __STM32F1xx_HAL_PWR_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"

void HAL_PWR_DeInit(void);
void HAL_PWR_EnableBkUpAccess(void);
void HAL_PWR_DisableBkUpAccess(void);

#ifdef __cplusplus
}
#endif

#endif
