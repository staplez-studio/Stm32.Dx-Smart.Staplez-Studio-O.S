/**
  ******************************************************************************
  * @file    stm32f1xx_hal_flash.c
  * @brief   FLASH HAL module driver.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"

HAL_StatusTypeDef HAL_FLASH_Unlock(void)
{
  FLASH->KEYR = 0x45670123U;
  FLASH->KEYR = 0xCDEF89ABU;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Lock(void)
{
  FLASH->CR |= 0x00000080U;
  return HAL_OK;
}
