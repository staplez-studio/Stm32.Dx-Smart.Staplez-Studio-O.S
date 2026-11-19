/**
  ******************************************************************************
  * @file    stm32f1xx_hal_cortex.c
  * @brief   CORTEX HAL module driver.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"

void HAL_NVIC_SetPriorityGrouping(uint32_t PriorityGroup)
{
  uint32_t reg_value;
  uint32_t PriorityGroupTmp = (PriorityGroup & (uint32_t)0x07UL);
  reg_value  =  SCB->AIRCR;
  reg_value &= ~((uint32_t)(0xFFFFUL << 16U) | (uint32_t)(7UL << 8U));
  reg_value  =  (reg_value | ((uint32_t)0x5FAUL << 16U) | (PriorityGroupTmp << 8U));
  SCB->AIRCR =  reg_value;
}

void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority)
{
  uint32_t prioritygroup = NVIC_GetPriorityGrouping();
  NVIC_SetPriority((int32_t)IRQn, NVIC_EncodePriority(prioritygroup, PreemptPriority, SubPriority));
}

void HAL_NVIC_EnableIRQ(IRQn_Type IRQn)
{
  if ((int32_t)IRQn >= 0) {
    NVIC->ISER[((uint32_t)IRQn) >> 5U] = (1UL << (((uint32_t)IRQn) & 0x1FU));
  }
}

void HAL_NVIC_DisableIRQ(IRQn_Type IRQn)
{
  if ((int32_t)IRQn >= 0) {
    NVIC->ICER[((uint32_t)IRQn) >> 5U] = (1UL << (((uint32_t)IRQn) & 0x1FU));
  }
}

void HAL_NVIC_SystemReset(void)
{
  __DSB();
  SCB->AIRCR  = ((0x5FAUL << 16U) | (SCB->AIRCR & (7UL << 8U)) | (1UL << 2U));
  __DSB();
  while(1) { __NOP(); }
}

uint32_t HAL_SYSTICK_Config(uint32_t TicksNumb)
{
  if ((TicksNumb - 1UL) > 0x00FFFFFFUL) { return (1UL); }
  SysTick->LOAD  = (uint32_t)(TicksNumb - 1UL);
  NVIC_SetPriority ((int32_t)SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
  SysTick->VAL   = 0UL;
  SysTick->CTRL  = (1UL << 2U) | (1UL << 1U) | 1UL;
  return (0UL);
}

void HAL_SYSTICK_CLKSourceConfig(uint32_t CLKSource)
{
  if (CLKSource == SYSTICK_CLKSOURCE_HCLK) {
    SysTick->CTRL |= SYSTICK_CLKSOURCE_HCLK;
  } else {
    SysTick->CTRL &= ~SYSTICK_CLKSOURCE_HCLK;
  }
}

void HAL_SYSTICK_IRQHandler(void)
{
  HAL_SYSTICK_Callback();
}

__weak void HAL_SYSTICK_Callback(void)
{
}
