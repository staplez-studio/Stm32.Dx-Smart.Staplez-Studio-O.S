/**
  ******************************************************************************
  * @file    stm32f1xx_hal_flash.h
  * @brief   Header file of FLASH HAL module.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_FLASH_H
#define __STM32F1xx_HAL_FLASH_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"

#define FLASH_LATENCY_0          0x00000000U
#define FLASH_LATENCY_1          0x00000001U
#define FLASH_LATENCY_2          0x00000002U

HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);

#include "stm32f1xx_hal_flash_ex.h"

#ifdef __cplusplus
}
#endif

#endif
