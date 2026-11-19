/**
  ******************************************************************************
  * @file    stm32f1xx_hal.c
  * @brief   HAL module driver.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"

static volatile uint32_t uwTick = 0;

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  (void)TickPriority;
  if (SystemCoreClock == 0) SystemCoreClock = 8000000U;
  SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;
  SysTick->VAL = 0U;
  SysTick->CTRL = 0x07U;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_Init(void)
{
  __HAL_AFIO_REMAP_SWJ_NOJTAG();
  SystemCoreClock = 8000000U;
  HAL_InitTick(0);
  /* Enable Cortex-M3 DWT Cycle Counter for microsecond timing (IR receiver) */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  DWT->CYCCNT = 0;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_DeInit(void)
{
  return HAL_OK;
}

void HAL_IncTick(void)
{
  uwTick++;
}

uint32_t HAL_GetTick(void)
{
  return uwTick;
}

void HAL_Delay(uint32_t Delay)
{
  uint32_t tickstart = HAL_GetTick();
  uint32_t wait = Delay;
  while ((HAL_GetTick() - tickstart) < wait) {
    __NOP();
  }
}

uint32_t HAL_GetHalVersion(void) { return 0x01080000U; }
uint32_t HAL_GetREVID(void)      { return 0x0000U; }
uint32_t HAL_GetDEVID(void)      { return 0x0414U; }
uint32_t HAL_GetUIDw0(void)      { return 0U; }
uint32_t HAL_GetUIDw1(void)      { return 0U; }
uint32_t HAL_GetUIDw2(void)      { return 0U; }
