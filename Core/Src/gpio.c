/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins (IR-Only Architecture).
  *          IR Input:    PA12 (Active-LOW EXTI falling edge with Pull-up)
  *          RTC SQW:     PA10 (Active-LOW EXTI falling edge with Pull-up)
  *          OLED I2C:    PB8 (SCL), PB9 (SDA)
  *          Status LED:  PC13
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "gpio.h"

void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();

  /* Configure GPIO pin Output Level : PC13 LED OFF (Active Low) */
  LED_OFF();

  /* Keep SWD active (disable JTAG) */
  __HAL_AFIO_REMAP_SWJ_NOJTAG();

  /* Configure the status LED pin (see main.h for the board choice) */
  GPIO_InitStruct.Pin = LED_BUILDIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_BUILDIN_GPIO_Port, &GPIO_InitStruct);

#if RTC_SQW_ENABLED
  /* DS3231 SQW 1 Hz falling-edge interrupt */
  GPIO_InitStruct.Pin = RTC_SQW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(RTC_SQW_GPIO_Port, &GPIO_InitStruct);
#endif

  /* Configure GPIO pin : PA12 (IR_RX Infrared Remote Receiver EXTI Falling) */
  GPIO_InitStruct.Pin = IR_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(IR_RX_GPIO_Port, &GPIO_InitStruct);

  /* Configure GPIO pin : PA8 (FAN_PWM_Pin - TIM1_CH1) */
  GPIO_InitStruct.Pin = FAN_PWM_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(FAN_PWM_GPIO_Port, &GPIO_InitStruct);

  /* Configure GPIO pin : PA9 for DFPlayer Mini Serial Command TX (Default HIGH idle) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* Configure GPIO pins : PA2 (USART2_TX) and PA3 (USART2_RX) for ESP32-CAM Wi-Fi */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP; // Essential: Internal pull-up prevents floating noise when Wi-Fi module is disconnected!
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* Configure GPIO pins : PB8 (I2C1_SCL) & PB9 (I2C1_SDA) with internal Pull-up for OLED & DS3231 */
  GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* Also configure PB7 with pull-up for fallback wiring */
  GPIO_InitStruct.Pin = GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* Configure GPIO pins : PB10 (USART3_TX) and PB11 (USART3_RX) for RDM6300 125kHz RFID */
  GPIO_InitStruct.Pin = RFID_TX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(RFID_TX_GPIO_Port, &GPIO_InitStruct);

#if !LED_ON_PB11
  /* Skipped when the status LED lives on this pin (DX-SMART PJ26: LED on PB11). */
  GPIO_InitStruct.Pin = RFID_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP; // Internal pull-up ensures clean idle state for 9600 8N1 serial stream
  HAL_GPIO_Init(RFID_RX_GPIO_Port, &GPIO_InitStruct);
#endif

  /* Configure GPIO pins for Texas Instruments PCM1808 24-bit 96kHz Stereo ADC (I2S2/SPI2) */
  /* PB12 = LRCK / Word Select, PB13 = BCK / Bit Clock, PB15 = DOUT / Serial Audio Data */
  GPIO_InitStruct.Pin = PCM1808_LRCK_Pin | PCM1808_BCK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PCM1808_DOUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(PCM1808_DOUT_GPIO_Port, &GPIO_InitStruct);

  /* Configure GPIO pins : PB5 (Passive Buzzer) & PB6 (Active Buzzer) - Initialize in silent High-Z Input mode
   * to eliminate power-on clicks and ground-loop feedback hum */
  GPIO_InitStruct.Pin = BUZZER_Pin | ACTIVE_BUZZER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BUZZER_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init for RTC SQW (PA10) and IR Receiver (PA12) */
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}
