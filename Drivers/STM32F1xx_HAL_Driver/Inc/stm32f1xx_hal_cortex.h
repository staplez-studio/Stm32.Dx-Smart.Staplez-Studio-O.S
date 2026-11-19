/**
  ******************************************************************************
  * @file    stm32f1xx_hal_cortex.h
  * @brief   Header file of CORTEX HAL module.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_CORTEX_H
#define __STM32F1xx_HAL_CORTEX_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"

#define NVIC_PRIORITYGROUP_0         0x00000007U
#define NVIC_PRIORITYGROUP_1         0x00000006U
#define NVIC_PRIORITYGROUP_2         0x00000005U
#define NVIC_PRIORITYGROUP_3         0x00000004U
#define NVIC_PRIORITYGROUP_4         0x00000003U

#define SYSTICK_CLKSOURCE_HCLK_DIV8 0x00000000U
#define SYSTICK_CLKSOURCE_HCLK      0x00000004U

void HAL_NVIC_SetPriorityGrouping(uint32_t PriorityGroup);
void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority);
void HAL_NVIC_EnableIRQ(IRQn_Type IRQn);
void HAL_NVIC_DisableIRQ(IRQn_Type IRQn);
void HAL_NVIC_SystemReset(void);
uint32_t HAL_SYSTICK_Config(uint32_t TicksNumb);
void HAL_SYSTICK_CLKSourceConfig(uint32_t CLKSource);
void HAL_SYSTICK_IRQHandler(void);
void HAL_SYSTICK_Callback(void);

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_HAL_CORTEX_H */
