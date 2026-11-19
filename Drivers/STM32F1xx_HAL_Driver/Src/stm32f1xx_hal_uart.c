/**
  ******************************************************************************
  * @file    stm32f1xx_hal_uart.c
  * @brief   UART HAL module driver.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"

/* APB1 clock (PPRE1 = RCC_CFGR[10:8]; 0xx = /1, 100 = /2, 101 = /4, 110 = /8, 111 = /16).
 * AHB is /1 in this project, so HCLK == SystemCoreClock. */
static uint32_t uart_get_pclk1(void)
{
  uint32_t ppre1 = (RCC->CFGR >> 8) & 0x7U;
  return (ppre1 < 4U) ? SystemCoreClock : (SystemCoreClock >> (ppre1 - 3U));
}

/* Only USART2 (APB1) is used in this project. USART1 would need the APB2 clock instead.
 * The old code hard-coded BRR = 0x0271 (625), which is 115200 baud only when the USART is fed
 * 72 MHz. USART2 gets 36 MHz, so it actually ran at 57,600 baud. */
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart)
{
  uint32_t baud = (huart->Init.BaudRate != 0U) ? huart->Init.BaudRate : 115200U;
  huart->Instance->BRR = (uart_get_pclk1() + (baud / 2U)) / baud;  /* rounded */
  huart->Instance->CR1 = 0x200C; // UE, TE, RE
  return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
  if (!huart || !huart->Instance || !pData || Size == 0) return HAL_ERROR;
  uint32_t tickstart = HAL_GetTick();
  for (uint16_t i = 0; i < Size; i++) {
    while (!(huart->Instance->SR & (1U << 7))) { // TXE flag
      if ((HAL_GetTick() - tickstart) >= Timeout) return HAL_TIMEOUT;
    }
    huart->Instance->DR = (uint16_t)pData[i];
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
  if (!huart || !huart->Instance || !pData || Size == 0) return HAL_ERROR;
  uint32_t tickstart = HAL_GetTick();
  for (uint16_t i = 0; i < Size; i++) {
    while (!(huart->Instance->SR & (1U << 5))) { // RXNE flag
      if ((HAL_GetTick() - tickstart) >= Timeout) return HAL_TIMEOUT;
    }
    pData[i] = (uint8_t)(huart->Instance->DR & 0xFF);
  }
  return HAL_OK;
}
