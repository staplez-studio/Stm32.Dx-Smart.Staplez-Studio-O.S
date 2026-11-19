/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   USART2 hardware implementation for ESP-01S Wi-Fi AT commands (PA2/PA3).
  ******************************************************************************
  */
/* USER CODE END Header */
#include "usart.h"
#include <string.h>

UART_HandleTypeDef huart2;

static uint32_t uart_pclk1(void)
{
  uint32_t ppre1 = (RCC->CFGR >> 8) & 0x7U;
  return (ppre1 < 4U) ? SystemCoreClock : (SystemCoreClock >> (ppre1 - 3U));
}

void MX_USART2_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_USART2_CLK_ENABLE();

  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  HAL_UART_Init(&huart2);

  USART2_ClearRX();
  USART2->CR1 |= (1U << 5);                 /* RXNEIE: interrupt on every received byte */
  HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);  /* below the IR edge interrupt (priority 0) */
  HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void USART2_SetBaud(uint32_t baud)
{
  if (baud == 0U) return;
  huart2.Init.BaudRate = baud;
  uint32_t cr1 = USART2->CR1;
  USART2->CR1 = cr1 & ~(1U << 13);                    /* UE off while BRR changes */
  USART2->BRR = (uart_pclk1() + (baud / 2U)) / baud;
  USART2->CR1 = cr1 | (1U << 13) | (1U << 5);         /* UE on, RXNEIE stays enabled */
  USART2_ClearRX();
}

uint32_t USART2_GetBaud(void) { return huart2.Init.BaudRate; }

void USART2_SendString(const char *str)
{
  if (!str) return;
  HAL_UART_Transmit(&huart2, (uint8_t*)str, (uint16_t)strlen(str), 50);
}

void USART2_SendData(const uint8_t *data, uint16_t len)
{
  if (!data || len == 0) return;
  HAL_UART_Transmit(&huart2, (uint8_t*)data, len, 50);
}

/* ---- Interrupt-driven receive ring -------------------------------------------------------
 * The old code polled the RXNE flag. At 115200 baud a byte arrives every 87 us, so any other
 * work (an OLED refresh takes tens of ms, a DFPlayer frame ~10 ms) silently lost bytes of the
 * module's reply. The ISR below just moves each byte into a 512 byte ring. */
#define USART2_RX_SIZE 512U   /* power of two */
static volatile uint8_t  rx_ring[USART2_RX_SIZE];
static volatile uint16_t rx_head = 0, rx_tail = 0;
volatile uint32_t usart2_rx_dropped = 0;
volatile uint32_t usart2_rx_junk = 0;      /* received bytes that are not printable text (a sign of the wrong baud rate) */
volatile uint32_t usart2_rx_total = 0;     /* every byte seen since start-up: tells "no wires" from "wrong baud" */

void USART2_RX_IRQHandler(void)
{
  uint32_t sr = USART2->SR;
  if (sr & ((1U << 5) | (1U << 3))) {                 /* RXNE or ORE */
    uint8_t b = (uint8_t)(USART2->DR & 0xFF);         /* reading SR then DR clears RXNE/ORE/NE/FE */
    usart2_rx_total++;
    if ((b < 9U) || (b > 13U && b < 32U) || (b > 126U)) usart2_rx_junk++;
    if (sr & (1U << 3)) usart2_rx_dropped++;          /* overrun: at least one byte was lost */
    uint16_t next = (uint16_t)((rx_head + 1U) & (USART2_RX_SIZE - 1U));
    if (next != rx_tail) {
      rx_ring[rx_head] = b;
      rx_head = next;
    } else {
      usart2_rx_dropped++;                            /* ring full */
    }
  }
}

bool USART2_ReadByte(uint8_t *out)
{
  if (rx_tail == rx_head) return false;
  *out = rx_ring[rx_tail];
  rx_tail = (uint16_t)((rx_tail + 1U) & (USART2_RX_SIZE - 1U));
  return true;
}

/* A partly received line is kept between calls: a timeout in the middle of a line must not hand the
 * caller half a line (that used to cut scan results in two). */
static char    rx_line[160];
static uint16_t rx_line_idx = 0;

void USART2_ClearRX(void)
{
  __disable_irq();
  rx_tail = rx_head;
  __enable_irq();
  rx_line_idx = 0;                                    /* a new command starts with a clean slate */
}

HAL_StatusTypeDef USART2_ReceiveLine(char *buffer, uint16_t max_len, uint32_t timeout_ms)
{
  if (!buffer || max_len == 0) return HAL_ERROR;
  uint32_t start = HAL_GetTick();
  while ((HAL_GetTick() - start) < timeout_ms) {
    uint8_t ch;
    if (!USART2_ReadByte(&ch)) continue;
    if (ch == '\r') continue;
    if (ch == '\n') {
      if (rx_line_idx > 0) {
        uint16_t n = (rx_line_idx < (uint16_t)(max_len - 1U)) ? rx_line_idx : (uint16_t)(max_len - 1U);
        memcpy(buffer, rx_line, n);
        buffer[n] = 0;
        rx_line_idx = 0;
        return HAL_OK;
      }
      continue;
    }
    if (rx_line_idx < sizeof(rx_line) - 1U) {
      rx_line[rx_line_idx++] = (char)ch;
    }
  }
  buffer[0] = 0;                                      /* timeout: nothing complete yet, partial line is kept */
  return HAL_TIMEOUT;
}
