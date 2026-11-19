/**
  ******************************************************************************
  * @file    stm32f1xx_hal_rcc.c
  * @brief   RCC HAL module driver.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"

HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *RCC_OscInitStruct)
{
  if (RCC_OscInitStruct == NULL) return HAL_ERROR;

  if (RCC_OscInitStruct->HSEState == RCC_HSE_ON) {
    /* Turn on HSE with sufficient startup timeout */
    RCC->CR |= (1U << 16); // HSEON
    uint32_t timeout = 200000;
    while (!(RCC->CR & (1U << 17)) && --timeout); // HSERDY
    if (timeout > 0 && RCC_OscInitStruct->PLL.PLLState == RCC_PLL_ON) {
      /* Configure FLASH latency 2 wait states for 72MHz */
      FLASH->ACR = (FLASH->ACR & ~0x07) | 0x02 | (1U << 4);
      /* APB1 = /2 (PPRE1 = 0b100 = 0x0400), APB2 = /1, AHB = /1, PLL = HSE * 9 = 72MHz */
      RCC->CFGR = (RCC->CFGR & ~((0x0F << 18) | (1U << 16) | (1U << 17) | (0x07 << 8)))
                | (0x07 << 18)
                | (1U << 16)
                | (0x04 << 8);
      RCC->CR |= (1U << 24); // PLLON
      timeout = 200000;
      while (!(RCC->CR & (1U << 25)) && --timeout); // PLLRDY
      if (timeout > 0) {
        RCC->CFGR = (RCC->CFGR & ~0x03) | 0x02; // SW = PLL
        timeout = 200000;
        while (((RCC->CFGR >> 2) & 0x03) != 0x02 && --timeout);
        SystemCoreClock = 72000000U;
        SysTick->LOAD = (72000000U / 1000U) - 1U;
        SysTick->VAL = 0U;
        SysTick->CTRL = 0x07U;
        return HAL_OK;
      }
    }
    return HAL_ERROR;
  } else if (RCC_OscInitStruct->HSIState == RCC_HSI_ON) {
    RCC->CR |= (1U << 0); // HSION
    uint32_t timeout = 100000;
    while (!(RCC->CR & (1U << 1)) && --timeout);
    if (RCC_OscInitStruct->PLL.PLLState == RCC_PLL_ON) {
      /* HSI / 2 * 9 = 36 MHz */
      FLASH->ACR = (FLASH->ACR & ~0x07) | 0x01 | (1U << 4);
      RCC->CFGR = (RCC->CFGR & ~((0x0F << 18) | (1U << 16) | (1U << 17) | (0x07 << 8)))
                | (0x07 << 18);
      RCC->CR |= (1U << 24); // PLLON
      timeout = 100000;
      while (!(RCC->CR & (1U << 25)) && --timeout);
      if (timeout > 0) {
        RCC->CFGR = (RCC->CFGR & ~0x03) | 0x02; // SW = PLL
        timeout = 100000;
        while (((RCC->CFGR >> 2) & 0x03) != 0x02 && --timeout);
        SystemCoreClock = 36000000U;
        SysTick->LOAD = (36000000U / 1000U) - 1U;
        SysTick->VAL = 0U;
        SysTick->CTRL = 0x07U;
        return HAL_OK;
      }
    }
    /* Fallback: Pure 8MHz HSI */
    RCC->CFGR = (RCC->CFGR & ~0x03);
    SystemCoreClock = 8000000U;
    SysTick->LOAD = (8000000U / 1000U) - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL = 0x07U;
    return HAL_OK;
  }
  return HAL_ERROR;
}

HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *RCC_ClkInitStruct, uint32_t FLatency)
{
  (void)RCC_ClkInitStruct; (void)FLatency;
  SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;
  SysTick->VAL = 0U;
  SysTick->CTRL = 0x07U;
  return HAL_OK;
}
