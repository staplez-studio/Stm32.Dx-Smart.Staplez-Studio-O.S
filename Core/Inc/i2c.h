/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.h
  * @brief   This file contains all the function prototypes for
  *          the i2c.c file
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __I2C_H__
#define __I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern I2C_HandleTypeDef hi2c1;

#ifndef I2C_MEMADD_SIZE_8BIT
#define I2C_MEMADD_SIZE_8BIT            0x00000001U
#endif
#ifndef I2C_MEMADD_SIZE_16BIT
#define I2C_MEMADD_SIZE_16BIT           0x00000002U
#endif

void MX_I2C1_Init(void);

#ifdef __cplusplus
}
#endif
#endif /*__ I2C_H__ */
