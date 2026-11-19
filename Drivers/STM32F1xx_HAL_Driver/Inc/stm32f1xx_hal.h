/**
  ******************************************************************************
  * @file    stm32f1xx_hal.h
  * @brief   This file contains all the functions prototypes for the HAL
  *          module driver.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_H
#define __STM32F1xx_HAL_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"
#include "stm32f1xx_hal_conf.h"
#include "system_stm32f1xx.h"

HAL_StatusTypeDef HAL_Init(void);
HAL_StatusTypeDef HAL_DeInit(void);
void HAL_MspInit(void);
void HAL_MspDeInit(void);
HAL_StatusTypeDef HAL_InitTick (uint32_t TickPriority);
void HAL_IncTick(void);
void HAL_Delay(uint32_t Delay);
uint32_t HAL_GetTick(void);

uint32_t HAL_GetHalVersion(void);
uint32_t HAL_GetREVID(void);
uint32_t HAL_GetDEVID(void);
uint32_t HAL_GetUIDw0(void);
uint32_t HAL_GetUIDw1(void);
uint32_t HAL_GetUIDw2(void);

#define __HAL_AFIO_REMAP_SWJ_NOJTAG() do { AFIO->MAPR = (AFIO->MAPR & ~0x07000000U) | 0x02000000U; } while(0)
#define __HAL_AFIO_REMAP_I2C1_ENABLE() do { AFIO->MAPR |= 0x00000002U; } while(0)

#define __HAL_RCC_GPIOA_CLK_ENABLE()   do { RCC->APB2ENR |= 0x00000004U; } while(0)
#define __HAL_RCC_GPIOB_CLK_ENABLE()   do { RCC->APB2ENR |= 0x00000008U; } while(0)
#define __HAL_RCC_GPIOC_CLK_ENABLE()   do { RCC->APB2ENR |= 0x00000010U; } while(0)
#define __HAL_RCC_GPIOD_CLK_ENABLE()   do { RCC->APB2ENR |= 0x00000020U; } while(0)
#define __HAL_RCC_AFIO_CLK_ENABLE()    do { RCC->APB2ENR |= 0x00000001U; } while(0)
#define __HAL_RCC_PWR_CLK_ENABLE()     do { RCC->APB1ENR |= 0x10000000U; } while(0)
#define __HAL_RCC_BKP_CLK_ENABLE()     do { RCC->APB1ENR |= 0x08000000U; } while(0)
#define __HAL_RCC_I2C1_CLK_ENABLE()    do { RCC->APB1ENR |= 0x00200000U; } while(0)
#define __HAL_RCC_TIM1_CLK_ENABLE()    do { RCC->APB2ENR |= 0x00000800U; } while(0)
#define __HAL_RCC_TIM2_CLK_ENABLE()    do { RCC->APB1ENR |= 0x00000001U; } while(0)
#define __HAL_RCC_USART1_CLK_ENABLE()  do { RCC->APB2ENR |= 0x00004000U; } while(0)
#define __HAL_RCC_USART2_CLK_ENABLE()  do { RCC->APB1ENR |= 0x00020000U; } while(0)
#define __HAL_RCC_DMA1_CLK_ENABLE()    do { RCC->AHBENR  |= 0x00000001U; } while(0)

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_HAL_H */
