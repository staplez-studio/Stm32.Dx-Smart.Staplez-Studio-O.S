/**
  ******************************************************************************
  * @file    stm32f1xx_hal_flash_ex.c
  * @brief   FLASH EX HAL module driver.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_flash_ex.h"

HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *pEraseInit, uint32_t *PageError)
{
  (void)pEraseInit; (void)PageError;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASHEx_Erase_IT(FLASH_EraseInitTypeDef *pEraseInit)
{
  (void)pEraseInit;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASHEx_OBProgram(FLASH_OBProgramInitTypeDef *pOBInit)
{
  (void)pOBInit;
  return HAL_OK;
}

void HAL_FLASHEx_OBGetConfig(FLASH_OBProgramInitTypeDef *pOBInit)
{
  (void)pOBInit;
}
