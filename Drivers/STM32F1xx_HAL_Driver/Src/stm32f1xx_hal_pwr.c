/**
  ******************************************************************************
  * @file    stm32f1xx_hal_pwr.c
  * @brief   PWR HAL module driver.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"

void HAL_PWR_DeInit(void) {}
void HAL_PWR_EnableBkUpAccess(void) { PWR->CR |= 0x0100U; }
void HAL_PWR_DisableBkUpAccess(void) { PWR->CR &= ~0x0100U; }
