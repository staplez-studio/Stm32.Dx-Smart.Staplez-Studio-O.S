/**
  ******************************************************************************
  * @file    system_stm32f1xx.c
  * @brief   CMSIS Cortex-M3 Device Peripheral Access Layer System Source File.
  ******************************************************************************
  */
#include "stm32f1xx.h"

uint32_t SystemCoreClock = 72000000U;

void SystemInit (void)
{
  RCC->CR |= 0x00000001U;
  RCC->CFGR &= 0xF8FF0000U;
  RCC->CR &= 0xFEF6FFFFU;
  RCC->CR &= 0xFFFBFFFFU;
  RCC->CFGR &= 0xFF80FFFFU;
  RCC->CIR = 0x009F0000U;
  SCB->VTOR = FLASH_BASE;
}

void SystemCoreClockUpdate (void)
{
  SystemCoreClock = 72000000U;
}
