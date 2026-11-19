/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    stm32f1xx_it.c
  * @brief   Interrupt Service Routines.
  *          Features PA10 RTC SQW and PA12 IR Receiver EXTI falling edge handlers.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "main.h"
#include "stm32f1xx_it.h"
#include "staplez_studio.h"
#include "ir_remote_app.h"
#include "usart.h"

void NMI_Handler(void) {}
void HardFault_Handler(void) { while(1); }
void MemManage_Handler(void) { while(1); }
void BusFault_Handler(void) { while(1); }
void UsageFault_Handler(void) { while(1); }
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void USART2_IRQHandler(void)
{
  USART2_RX_IRQHandler();
}

void EXTI15_10_IRQHandler(void)
{
#if RTC_SQW_ENABLED
  if (__HAL_GPIO_EXTI_GET_IT(RTC_SQW_Pin) != RESET) {
    __HAL_GPIO_EXTI_CLEAR_IT(RTC_SQW_Pin);
    /* 1 Hz tick from the DS3231. Only flag it here. The I2C bus is bit-banged and shared with
     * the OLED, so reading the RTC from interrupt context would corrupt whatever transfer the
     * main loop is in the middle of, and would also block IR edge capture, which shares this
     * vector. Staplez_Process() does the actual read. */
    rtc_sqw_pending = 1;
  }
#endif

  if (__HAL_GPIO_EXTI_GET_IT(IR_RX_Pin) != RESET) {
    __HAL_GPIO_EXTI_CLEAR_IT(IR_RX_Pin);
    // Real-time IR Remote pulse detection
    IR_Remote_EXTI_Callback();
  }
}
