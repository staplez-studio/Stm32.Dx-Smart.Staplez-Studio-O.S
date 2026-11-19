/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

void Error_Handler(void);

/* Pin Definitions matching SSOS.ioc (IR-Only Architecture) */
/* ---- Board-specific choices -------------------------------------------------------------
 * Default target: DX-SMART PJ26 (STM32F103C8T6 + CH340X USB-serial, LoRa socket).
 *   - Only user LED is blue LED2 on PB11, wired to 3.3 V through 1k to GND => ACTIVE HIGH.
 *   - The CH340X drives PA10 (MCU RX) all the time, so the DS3231 SQW interrupt cannot share
 *     PA10 on this board. Leave RTC_SQW_ENABLED 0 (the RTC is polled over I2C), or wire SQW to
 *     a free pin, change RTC_SQW_Pin/Port/EXTI below, and set it to 1.
 * Classic Blue Pill: LED on PC13, active LOW -> set LED_ON_PB11 to 0. */
#ifndef LED_ON_PB11
#define LED_ON_PB11           1   /* 1 = DX-SMART PJ26 (PB11, active high); 0 = classic Blue Pill (PC13, active low) */
#endif
#if LED_ON_PB11
#define LED_BUILDIN_Pin       GPIO_PIN_11
#define LED_BUILDIN_GPIO_Port GPIOB
#define LED_ACTIVE_LOW        0
#else
#define LED_BUILDIN_Pin       GPIO_PIN_13
#define LED_BUILDIN_GPIO_Port GPIOC
#define LED_ACTIVE_LOW        1
#endif

#define LED_ON()      HAL_GPIO_WritePin(LED_BUILDIN_GPIO_Port, LED_BUILDIN_Pin, LED_ACTIVE_LOW ? GPIO_PIN_RESET : GPIO_PIN_SET)
#define LED_OFF()     HAL_GPIO_WritePin(LED_BUILDIN_GPIO_Port, LED_BUILDIN_Pin, LED_ACTIVE_LOW ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define LED_TOGGLE()  HAL_GPIO_TogglePin(LED_BUILDIN_GPIO_Port, LED_BUILDIN_Pin)

#ifndef RTC_SQW_ENABLED
#define RTC_SQW_ENABLED       0
#endif

#define RTC_SQW_Pin GPIO_PIN_10
#define RTC_SQW_GPIO_Port GPIOA
#define RTC_SQW_EXTI_IRQn EXTI15_10_IRQn

#define IR_RX_Pin GPIO_PIN_12
#define IR_RX_GPIO_Port GPIOA
#define IR_RX_EXTI_IRQn EXTI15_10_IRQn

#define FAN_PWM_Pin GPIO_PIN_8
#define FAN_PWM_GPIO_Port GPIOA

#define BUZZER_Pin GPIO_PIN_5
#define BUZZER_GPIO_Port GPIOB

#define ACTIVE_BUZZER_Pin GPIO_PIN_6
#define ACTIVE_BUZZER_GPIO_Port GPIOB

/* Texas Instruments PCM1808 24-bit 96kHz Stereo ADC (I2S2 / SPI2) */
#define PCM1808_LRCK_Pin GPIO_PIN_12
#define PCM1808_LRCK_GPIO_Port GPIOB
#define PCM1808_BCK_Pin GPIO_PIN_13
#define PCM1808_BCK_GPIO_Port GPIOB
#define PCM1808_DOUT_Pin GPIO_PIN_15
#define PCM1808_DOUT_GPIO_Port GPIOB

/* RDM6300 125kHz Read-Only RFID Receiver (USART3 9600 8N1) */
#define RFID_TX_Pin GPIO_PIN_10
#define RFID_TX_GPIO_Port GPIOB
#define RFID_RX_Pin GPIO_PIN_11
#define RFID_RX_GPIO_Port GPIOB

/* ESP-01S / ESP32-CAM Wi-Fi AT Interface (USART2 115200 8N1) */
#define WIFI_TX_Pin GPIO_PIN_2
#define WIFI_TX_GPIO_Port GPIOA
#define WIFI_RX_Pin GPIO_PIN_3
#define WIFI_RX_GPIO_Port GPIOA

/* I2C1 Bus (SH1106 OLED 0x3C & DS3231 RTC 0x68) */
#define OLED_SCL_Pin GPIO_PIN_8
#define OLED_SCL_GPIO_Port GPIOB
#define OLED_SDA_Pin GPIO_PIN_9
#define OLED_SDA_GPIO_Port GPIOB

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
