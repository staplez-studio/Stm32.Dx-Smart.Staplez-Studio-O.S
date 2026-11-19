/**
  ******************************************************************************
  * @file    stm32f1xx_hal_rcc.h
  * @brief   Header file of RCC HAL module.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_RCC_H
#define __STM32F1xx_HAL_RCC_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"

#define RCC_OSCILLATORTYPE_NONE      0x00000000U
#define RCC_OSCILLATORTYPE_HSE       0x00000001U
#define RCC_OSCILLATORTYPE_HSI       0x00000002U
#define RCC_OSCILLATORTYPE_LSE       0x00000004U
#define RCC_OSCILLATORTYPE_LSI       0x00000008U

#define RCC_HSE_OFF                  0x00000000U
#define RCC_HSE_ON                   0x00010000U
#define RCC_HSE_PREDIV_DIV1          0x00000000U

#define RCC_HSI_OFF                  0x00000000U
#define RCC_HSI_ON                   0x00000001U
#define RCC_HSICALIBRATION_DEFAULT   0x10U

#define RCC_PLL_NONE                 0x00000000U
#define RCC_PLL_OFF                  0x00000001U
#define RCC_PLL_ON                   0x00000002U

#define RCC_PLLSOURCE_HSI_DIV2       0x00000000U
#define RCC_PLLSOURCE_HSE            0x00010000U

#define RCC_PLL_MUL2                 0x00000000U
#define RCC_PLL_MUL3                 0x00040000U
#define RCC_PLL_MUL4                 0x00080000U
#define RCC_PLL_MUL5                 0x000C0000U
#define RCC_PLL_MUL6                 0x00100000U
#define RCC_PLL_MUL7                 0x00140000U
#define RCC_PLL_MUL8                 0x00180000U
#define RCC_PLL_MUL9                 0x001C0000U
#define RCC_PLL_MUL10                0x00200000U
#define RCC_PLL_MUL11                0x00240000U
#define RCC_PLL_MUL12                0x00280000U
#define RCC_PLL_MUL13                0x002C0000U
#define RCC_PLL_MUL14                0x00300000U
#define RCC_PLL_MUL15                0x00340000U
#define RCC_PLL_MUL16                0x00380000U

#define RCC_CLOCKTYPE_SYSCLK         0x00000001U
#define RCC_CLOCKTYPE_HCLK           0x00000002U
#define RCC_CLOCKTYPE_PCLK1          0x00000004U
#define RCC_CLOCKTYPE_PCLK2          0x00000008U

#define RCC_SYSCLKSOURCE_HSI         0x00000000U
#define RCC_SYSCLKSOURCE_HSE         0x00000001U
#define RCC_SYSCLKSOURCE_PLLCLK      0x00000002U

#define RCC_SYSCLK_DIV1              0x00000000U
#define RCC_HCLK_DIV1                0x00000000U
#define RCC_HCLK_DIV2                0x00000400U

typedef struct {
  uint32_t PLLState;
  uint32_t PLLSource;
  uint32_t PLLMUL;
} RCC_PLLInitTypeDef;

typedef struct {
  uint32_t OscillatorType;
  uint32_t HSEState;
  uint32_t HSEPredivValue;
  uint32_t HSIState;
  uint32_t HSICalibrationValue;
  RCC_PLLInitTypeDef PLL;
} RCC_OscInitTypeDef;

typedef struct {
  uint32_t ClockType;
  uint32_t SYSCLKSource;
  uint32_t AHBCLKDivider;
  uint32_t APB1CLKDivider;
  uint32_t APB2CLKDivider;
} RCC_ClkInitTypeDef;

HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef  *RCC_OscInitStruct);
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef  *RCC_ClkInitStruct, uint32_t FLatency);

#include "stm32f1xx_hal_rcc_ex.h"

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_HAL_RCC_H */
