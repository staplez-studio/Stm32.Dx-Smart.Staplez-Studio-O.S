/**
  ******************************************************************************
  * @file    stm32f1xx_hal_rcc_ex.c
  * @brief   RCC EX HAL module driver.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_rcc_ex.h"

HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *PeriphClkInit)
{
  (void)PeriphClkInit;
  return HAL_OK;
}

void HAL_RCCEx_GetPeriphCLKConfig(RCC_PeriphCLKInitTypeDef *PeriphClkInit)
{
  (void)PeriphClkInit;
}

uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t PeriphClk)
{
  (void)PeriphClk;
  return 0U;
}
