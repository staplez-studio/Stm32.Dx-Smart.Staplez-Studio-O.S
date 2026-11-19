/**
  ******************************************************************************
  * @file    stm32f1xx_hal_i2c.h
  * @brief   Header file of I2C HAL module.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_I2C_H
#define __STM32F1xx_HAL_I2C_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"

#define I2C_MEMADD_SIZE_8BIT            0x00000001U
#define I2C_MEMADD_SIZE_16BIT           0x00000002U

#define I2C_FIRST_FRAME                 0x00000001U
#define I2C_NEXT_FRAME                  0x00000002U
#define I2C_LAST_FRAME                  0x00000004U

#define I2C_DUTYCYCLE_2                 0x00000000U
#define I2C_DUTYCYCLE_16_9              0x00004000U

#define I2C_ADDRESSINGMODE_7BIT         0x00004000U
#define I2C_ADDRESSINGMODE_10BIT        0x0000C000U

#define I2C_DUALADDRESS_DISABLE         0x00000000U
#define I2C_DUALADDRESS_ENABLE          0x00000001U

#define I2C_GENERALCALL_DISABLE         0x00000000U
#define I2C_GENERALCALL_ENABLE          0x00000040U

#define I2C_NOSTRETCH_DISABLE           0x00000000U
#define I2C_NOSTRETCH_ENABLE            0x00000080U

typedef struct {
  uint32_t ClockSpeed;
  uint32_t DutyCycle;
  uint32_t OwnAddress1;
  uint32_t AddressingMode;
  uint32_t DualAddressMode;
  uint32_t OwnAddress2;
  uint32_t GeneralCallMode;
  uint32_t NoStretchMode;
} I2C_InitTypeDef;

typedef struct {
  I2C_TypeDef                *Instance;
  I2C_InitTypeDef            Init;
  HAL_LockTypeDef            Lock;
  __IO uint32_t              State;
  __IO uint32_t              ErrorCode;
} I2C_HandleTypeDef;

HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint32_t Trials, uint32_t Timeout);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout);

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_HAL_I2C_H */
