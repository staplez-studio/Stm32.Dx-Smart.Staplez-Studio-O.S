/**
  ******************************************************************************
  * @file    system_stm32f1xx.h
  * @brief   CMSIS Cortex-M3 Device System Header File.
  ******************************************************************************
  */
#ifndef __SYSTEM_STM32F1XX_H
#define __SYSTEM_STM32F1XX_H

#ifdef __cplusplus
 extern "C" {
#endif

extern uint32_t SystemCoreClock;

extern void SystemInit(void);
extern void SystemCoreClockUpdate(void);

#ifdef __cplusplus
}
#endif

#endif /*__SYSTEM_STM32F1XX_H */
