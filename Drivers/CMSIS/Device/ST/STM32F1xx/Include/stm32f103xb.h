/**
  ******************************************************************************
  * @file    stm32f103xb.h
  * @brief   CMSIS Cortex-M3 Device Peripheral Access Layer Header File.
  ******************************************************************************
  */
#ifndef __STM32F103xB_H
#define __STM32F103xB_H

#ifdef __cplusplus
 extern "C" {
#endif

typedef enum {
  NonMaskableInt_IRQn   = -14,
  HardFault_IRQn        = -13,
  MemoryManagement_IRQn = -12,
  BusFault_IRQn         = -11,
  UsageFault_IRQn       = -10,
  SVCall_IRQn           = -5,
  DebugMonitor_IRQn     = -4,
  PendSV_IRQn           = -2,
  SysTick_IRQn          = -1,
  WWDG_IRQn             = 0,
  PVD_IRQn              = 1,
  TAMPER_IRQn           = 2,
  RTC_IRQn              = 3,
  FLASH_IRQn            = 4,
  RCC_IRQn              = 5,
  EXTI0_IRQn            = 6,
  EXTI1_IRQn            = 7,
  EXTI2_IRQn            = 8,
  EXTI3_IRQn            = 9,
  EXTI4_IRQn            = 10,
  DMA1_Channel1_IRQn    = 11,
  DMA1_Channel2_IRQn    = 12,
  DMA1_Channel3_IRQn    = 13,
  DMA1_Channel4_IRQn    = 14,
  DMA1_Channel5_IRQn    = 15,
  DMA1_Channel6_IRQn    = 16,
  DMA1_Channel7_IRQn    = 17,
  ADC1_2_IRQn           = 18,
  USB_HP_CAN1_TX_IRQn   = 19,
  USB_LP_CAN1_RX0_IRQn  = 20,
  CAN1_RX1_IRQn         = 21,
  CAN1_SCE_IRQn         = 22,
  EXTI9_5_IRQn          = 23,
  TIM1_BRK_IRQn         = 24,
  TIM1_UP_IRQn          = 25,
  TIM1_TRG_COM_IRQn     = 26,
  TIM1_CC_IRQn          = 27,
  TIM2_IRQn             = 28,
  TIM3_IRQn             = 29,
  TIM4_IRQn             = 30,
  I2C1_EV_IRQn          = 31,
  I2C1_ER_IRQn          = 32,
  I2C2_EV_IRQn          = 33,
  I2C2_ER_IRQn          = 34,
  SPI1_IRQn             = 35,
  SPI2_IRQn             = 36,
  USART1_IRQn           = 37,
  USART2_IRQn           = 38,
  USART3_IRQn           = 39,
  EXTI15_10_IRQn        = 40,
  RTC_Alarm_IRQn        = 41,
  USBWakeUp_IRQn        = 42
} IRQn_Type;

#include "core_cm3.h"
#include <stdint.h>

typedef struct {
  volatile uint32_t CRL;
  volatile uint32_t CRH;
  volatile uint32_t IDR;
  volatile uint32_t ODR;
  volatile uint32_t BSRR;
  volatile uint32_t BRR;
  volatile uint32_t LCKR;
} GPIO_TypeDef;

typedef struct {
  volatile uint32_t CR1;
  volatile uint32_t CR2;
  volatile uint32_t OAR1;
  volatile uint32_t OAR2;
  volatile uint32_t DR;
  volatile uint32_t SR1;
  volatile uint32_t SR2;
  volatile uint32_t CCR;
  volatile uint32_t TRISE;
} I2C_TypeDef;

typedef struct {
  volatile uint32_t SR;
  volatile uint32_t DR;
  volatile uint32_t BRR;
  volatile uint32_t CR1;
  volatile uint32_t CR2;
  volatile uint32_t CR3;
  volatile uint32_t GTPR;
} USART_TypeDef;

typedef struct {
  volatile uint32_t CR1;
  volatile uint32_t CR2;
  volatile uint32_t SMCR;
  volatile uint32_t DIER;
  volatile uint32_t SR;
  volatile uint32_t EGR;
  volatile uint32_t CCMR1;
  volatile uint32_t CCMR2;
  volatile uint32_t CCER;
  volatile uint32_t CNT;
  volatile uint32_t PSC;
  volatile uint32_t ARR;
  volatile uint32_t RCR;
  volatile uint32_t CCR1;
  volatile uint32_t CCR2;
  volatile uint32_t CCR3;
  volatile uint32_t CCR4;
  volatile uint32_t BDTR;
  volatile uint32_t DCR;
  volatile uint32_t DMAR;
} TIM_TypeDef;

typedef struct {
  volatile uint32_t CR;
  volatile uint32_t CFGR;
  volatile uint32_t CIR;
  volatile uint32_t APB2RSTR;
  volatile uint32_t APB1RSTR;
  volatile uint32_t AHBENR;
  volatile uint32_t APB2ENR;
  volatile uint32_t APB1ENR;
  volatile uint32_t BDCR;
  volatile uint32_t CSR;
} RCC_TypeDef;

typedef struct {
  volatile uint32_t EVCR;
  volatile uint32_t MAPR;
  volatile uint32_t EXTICR[4];
  volatile uint32_t MAPR2;
} AFIO_TypeDef;

typedef struct {
  volatile uint32_t CR;
  volatile uint32_t CSR;
} PWR_TypeDef;

typedef struct {
  volatile uint32_t ACR;
  volatile uint32_t KEYR;
  volatile uint32_t OPTKEYR;
  volatile uint32_t SR;
  volatile uint32_t CR;
  volatile uint32_t AR;
  volatile uint32_t RESERVED;
  volatile uint32_t OBR;
  volatile uint32_t WRPR;
} FLASH_TypeDef;

typedef struct {
  volatile uint32_t IMR;
  volatile uint32_t EMR;
  volatile uint32_t RTSR;
  volatile uint32_t FTSR;
  volatile uint32_t SWIER;
  volatile uint32_t PR;
} EXTI_TypeDef;

typedef struct {
  volatile uint32_t CCR;
  volatile uint32_t CNDTR;
  volatile uint32_t CPAR;
  volatile uint32_t CMAR;
} DMA_Channel_TypeDef;

typedef struct {
  volatile uint32_t RESERVED0;
  volatile uint32_t DR1;
  volatile uint32_t DR2;
  volatile uint32_t DR3;
  volatile uint32_t DR4;
  volatile uint32_t DR5;
  volatile uint32_t DR6;
  volatile uint32_t DR7;
  volatile uint32_t DR8;
  volatile uint32_t DR9;
  volatile uint32_t DR10;
  volatile uint32_t RTCCR;
  volatile uint32_t CR;
  volatile uint32_t CSR;
} BKP_TypeDef;

#define FLASH_BASE            (0x08000000UL)
#define SRAM_BASE             (0x20000000UL)
#define PERIPH_BASE           (0x40000000UL)

#define APB1PERIPH_BASE       PERIPH_BASE
#define APB2PERIPH_BASE       (PERIPH_BASE + 0x00010000UL)
#define AHBPERIPH_BASE        (PERIPH_BASE + 0x00020000UL)

#define TIM1_BASE             (APB2PERIPH_BASE + 0x00002C00UL)
#define USART2_BASE           (APB1PERIPH_BASE + 0x00004400UL)
#define I2C1_BASE             (APB1PERIPH_BASE + 0x00005400UL)
#define BKP_BASE              (APB1PERIPH_BASE + 0x00006C00UL)

#define AFIO_BASE             (APB2PERIPH_BASE + 0x00000000UL)
#define EXTI_BASE             (APB2PERIPH_BASE + 0x00000400UL)
#define GPIOA_BASE            (APB2PERIPH_BASE + 0x00000800UL)
#define GPIOB_BASE            (APB2PERIPH_BASE + 0x00000C00UL)
#define GPIOC_BASE            (APB2PERIPH_BASE + 0x00001000UL)
#define GPIOD_BASE            (APB2PERIPH_BASE + 0x00001400UL)
#define PWR_BASE              (APB1PERIPH_BASE + 0x00007000UL)

#define DMA1_BASE             (AHBPERIPH_BASE + 0x00000000UL)
#define RCC_BASE              (AHBPERIPH_BASE + 0x00001000UL)
#define FLASH_R_BASE          (AHBPERIPH_BASE + 0x00002000UL)

#define GPIOA                 ((GPIO_TypeDef *) GPIOA_BASE)
#define GPIOB                 ((GPIO_TypeDef *) GPIOB_BASE)
#define GPIOC                 ((GPIO_TypeDef *) GPIOC_BASE)
#define GPIOD                 ((GPIO_TypeDef *) GPIOD_BASE)
#define AFIO                  ((AFIO_TypeDef *) AFIO_BASE)
#define EXTI                  ((EXTI_TypeDef *) EXTI_BASE)
#define RCC                   ((RCC_TypeDef *) RCC_BASE)
#define I2C1                  ((I2C_TypeDef *) I2C1_BASE)
#define USART2                ((USART_TypeDef *) USART2_BASE)
#define TIM1                  ((TIM_TypeDef *) TIM1_BASE)
#define PWR                   ((PWR_TypeDef *) PWR_BASE)
#define BKP                   ((BKP_TypeDef *) BKP_BASE)
#define FLASH                 ((FLASH_TypeDef *) FLASH_R_BASE)
#define DMA1                  ((DMA_TypeDef *) DMA1_BASE)

#include "system_stm32f1xx.h"

#ifdef __cplusplus
}
#endif

#endif /* __STM32F103xB_H */
