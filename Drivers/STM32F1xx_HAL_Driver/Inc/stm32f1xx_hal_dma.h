/**
  ******************************************************************************
  * @file    stm32f1xx_hal_dma.h
  * @brief   Header file of DMA HAL module.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_DMA_H
#define __STM32F1xx_HAL_DMA_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"

typedef struct {
  DMA_Channel_TypeDef   *Instance;
  HAL_LockTypeDef       Lock;
  __IO uint32_t         State;
} DMA_HandleTypeDef;

HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *hdma);
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *hdma);

#include "stm32f1xx_hal_dma_ex.h"

#ifdef __cplusplus
}
#endif

#endif
