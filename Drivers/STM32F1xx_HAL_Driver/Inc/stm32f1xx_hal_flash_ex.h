/**
  ******************************************************************************
  * @file    stm32f1xx_hal_flash_ex.h
  * @brief   Header file of FLASH EX HAL module.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_FLASH_EX_H
#define __STM32F1xx_HAL_FLASH_EX_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"

typedef struct {
  uint32_t TypeErase;
  uint32_t Banks;
  uint32_t PageAddress;
  uint32_t NbPages;
} FLASH_EraseInitTypeDef;

typedef struct {
  uint32_t OptionType;
  uint32_t WRPState;
  uint32_t WRPPage;
  uint32_t Banks;
  uint8_t  RDPLevel;
  uint8_t  USERConfig;
  uint32_t DataAddress;
  uint8_t  DataValue;
} FLASH_OBProgramInitTypeDef;

#define FLASH_TYPEERASE_PAGES     0x00U
#define FLASH_TYPEERASE_MASSERASE 0x02U

HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *pEraseInit, uint32_t *PageError);
HAL_StatusTypeDef HAL_FLASHEx_Erase_IT(FLASH_EraseInitTypeDef *pEraseInit);
HAL_StatusTypeDef HAL_FLASHEx_OBProgram(FLASH_OBProgramInitTypeDef *pOBInit);
void              HAL_FLASHEx_OBGetConfig(FLASH_OBProgramInitTypeDef *pOBInit);

#ifdef __cplusplus
}
#endif

#endif
