/**
  ******************************************************************************
  * @file    stm32f1xx.h
  * @brief   CMSIS STM32F1xx Device Peripheral Access Layer Header File.
  ******************************************************************************
  */
#ifndef __STM32F1xx_H
#define __STM32F1xx_H

#ifdef __cplusplus
 extern "C" {
#endif

#if !defined (STM32F103xB) && !defined (STM32F103x6) && !defined (STM32F103xE)
  #define STM32F103xB
#endif

#if defined(STM32F103xB)
  #include "stm32f103xb.h"
#endif

typedef enum {
  RESET = 0,
  SET = !RESET
} FlagStatus, ITStatus;

typedef enum {
  DISABLE = 0,
  ENABLE = !DISABLE
} FunctionalState;

typedef enum {
  ERROR = 0,
  SUCCESS = !ERROR
} ErrorStatus;

#define SET_BIT(REG, BIT)     ((REG) |= (BIT))
#define CLEAR_BIT(REG, BIT)   ((REG) &= ~(BIT))
#define READ_BIT(REG, BIT)    ((REG) & (BIT))
#define CLEAR_REG(REG)        ((REG) = (0x0))
#define WRITE_REG(REG, VAL)   ((REG) = (VAL))
#define READ_REG(REG)         ((REG))
#define MODIFY_REG(REG, CLEARMASK, SETMASK)  WRITE_REG((REG), (((READ_REG(REG)) & (~(CLEARMASK))) | (SETMASK)))

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_H */
