/**
  ******************************************************************************
  * @file    stm32f1xx_hal_rcc_ex.h
  * @brief   Header file of RCC EX HAL module.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_RCC_EX_H
#define __STM32F1xx_HAL_RCC_EX_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"

#define RCC_PERIPHCLK_RTC            0x00000001U
#define RCC_PERIPHCLK_ADC            0x00000002U
#define RCC_PERIPHCLK_USB            0x00000004U

#define RCC_RTCCLKSOURCE_NO_CLK      0x00000000U
#define RCC_RTCCLKSOURCE_LSE         0x00000100U
#define RCC_RTCCLKSOURCE_LSI         0x00000200U
#define RCC_RTCCLKSOURCE_HSE_DIV128  0x00000300U

#define RCC_ADCPCLK2_DIV2            0x00000000U
#define RCC_ADCPCLK2_DIV4            0x00004000U
#define RCC_ADCPCLK2_DIV6            0x00008000U
#define RCC_ADCPCLK2_DIV8            0x0000C000U

#define RCC_USBCLKSOURCE_PLL_DIV1_5  0x00000000U
#define RCC_USBCLKSOURCE_PLL         0x00400000U

typedef struct {
  uint32_t PeriphClockSelection;
  uint32_t RTCClockSelection;
  uint32_t AdcClockSelection;
  uint32_t UsbClockSelection;
} RCC_PeriphCLKInitTypeDef;

HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef  *PeriphClkInit);
void              HAL_RCCEx_GetPeriphCLKConfig(RCC_PeriphCLKInitTypeDef  *PeriphClkInit);
uint32_t          HAL_RCCEx_GetPeriphCLKFreq(uint32_t PeriphClk);

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_HAL_RCC_EX_H */
