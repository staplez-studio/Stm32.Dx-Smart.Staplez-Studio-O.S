/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.h
  * @brief   USART2 driver for ESP-01S Wi-Fi module & serial telemetry (PA2 TX / PA3 RX).
  *          Default baud rate: 115200 8N1 (standard for Espressif AT firmware).
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

extern UART_HandleTypeDef huart2;

void MX_USART2_Init(void);
void USART2_SetBaud(uint32_t baud);          /* change speed at run time (keeps the RX interrupt on) */
uint32_t USART2_GetBaud(void);
extern volatile uint32_t usart2_rx_junk;     /* bytes that are not printable text */
extern volatile uint32_t usart2_rx_total;    /* bytes received since start-up */
void USART2_SendString(const char *str);
void USART2_SendData(const uint8_t *data, uint16_t len);
HAL_StatusTypeDef USART2_ReceiveLine(char *buffer, uint16_t max_len, uint32_t timeout_ms);
void USART2_ClearRX(void);
bool USART2_ReadByte(uint8_t *out);          /* non-blocking: false if the ring is empty */
void USART2_RX_IRQHandler(void);             /* call from USART2_IRQHandler() */
extern volatile uint32_t usart2_rx_dropped;  /* bytes lost to a full ring or an overrun */

#ifdef __cplusplus
}
#endif
#endif /*__ USART_H__ */
