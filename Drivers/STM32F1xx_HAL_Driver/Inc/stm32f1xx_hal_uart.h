/**
  ******************************************************************************
  * @file    stm32f1xx_hal_uart.h
  * @brief   Header file of UART HAL module.
  ******************************************************************************
  */
#ifndef __STM32F1xx_HAL_UART_H
#define __STM32F1xx_HAL_UART_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal_def.h"

#define UART_WORDLENGTH_8B                  0x00000000U
#define UART_STOPBITS_1                     0x00000000U
#define UART_PARITY_NONE                    0x00000000U
#define UART_MODE_TX_RX                     0x0000000CU
#define UART_HWCONTROL_NONE                 0x00000000U
#define UART_OVERSAMPLING_16                0x00000000U

/* UART Flags */
#define UART_FLAG_PE                        0x00000001U
#define UART_FLAG_FE                        0x00000002U
#define UART_FLAG_NE                        0x00000004U
#define UART_FLAG_ORE                       0x00000008U
#define UART_FLAG_IDLE                      0x00000010U
#define UART_FLAG_RXNE                      0x00000020U
#define UART_FLAG_TC                        0x00000040U
#define UART_FLAG_TXE                       0x00000080U

#define __HAL_UART_GET_FLAG(__HANDLE__, __FLAG__) (((__HANDLE__)->Instance->SR & (__FLAG__)) == (__FLAG__))
#define __HAL_UART_CLEAR_FLAG(__HANDLE__, __FLAG__) do {   volatile uint32_t __tmpsr = (__HANDLE__)->Instance->SR;   volatile uint32_t __tmpdr = (__HANDLE__)->Instance->DR;   (void)__tmpsr;   (void)__tmpdr; } while(0)

typedef struct {
  uint32_t BaudRate;
  uint32_t WordLength;
  uint32_t StopBits;
  uint32_t Parity;
  uint32_t Mode;
  uint32_t HwFlowCtl;
  uint32_t OverSampling;
} UART_InitTypeDef;

typedef struct {
  USART_TypeDef                      *Instance;
  UART_InitTypeDef                   Init;
  HAL_LockTypeDef                    Lock;
  __IO uint32_t                      State;
  __IO uint32_t                      ErrorCode;
} UART_HandleTypeDef;

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout);

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_HAL_UART_H */
