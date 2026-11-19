/**
  ******************************************************************************
  * @file    stm32f1xx_hal_gpio.c
  * @brief   GPIO HAL module driver.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"

void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init)
{
  uint32_t config = 0x03; // default push-pull 50MHz
  uint8_t is_input = 0;
  if ((GPIO_Init->Mode & 0x10000000U) || GPIO_Init->Mode == GPIO_MODE_INPUT) {
    is_input = 1;
    config = (GPIO_Init->Pull == GPIO_NOPULL) ? 0x04 : 0x08; // 0x08 = input with pull-up/pull-down
  } else if (GPIO_Init->Mode == GPIO_MODE_OUTPUT_OD) {
    config = 0x07; // General purpose output open-drain 50MHz
  } else if (GPIO_Init->Mode == GPIO_MODE_AF_OD) {
    config = 0x0F; // Alternate function output open-drain 50MHz
  } else if (GPIO_Init->Mode == GPIO_MODE_AF_PP) {
    config = 0x0B; // Alternate function output push-pull 50MHz
  } else if (GPIO_Init->Mode == GPIO_MODE_OUTPUT_PP) {
    config = 0x03; // General purpose output push-pull 50MHz
  }

  for (uint32_t pinpos = 0; pinpos < 16; pinpos++) {
    if (GPIO_Init->Pin & (1U << pinpos)) {
      if (pinpos < 8) {
        GPIOx->CRL = (GPIOx->CRL & ~(0xF << (pinpos * 4))) | (config << (pinpos * 4));
      } else {
        GPIOx->CRH = (GPIOx->CRH & ~(0xF << ((pinpos - 8) * 4))) | (config << ((pinpos - 8) * 4));
      }
      if (is_input && GPIO_Init->Pull == GPIO_PULLUP) {
        GPIOx->BSRR = (1U << pinpos);
      } else if (is_input && GPIO_Init->Pull == GPIO_PULLDOWN) {
        GPIOx->BRR = (1U << pinpos);
      }

      /* Handle EXTI Interrupt Configuration */
      if (GPIO_Init->Mode & 0x10000000U) {
        RCC->APB2ENR |= 0x00000001U; // Enable AFIO clock
        EXTI->IMR |= (1U << pinpos);
        if (GPIO_Init->Mode & 0x00100000U) {
          EXTI->RTSR |= (1U << pinpos);
        }
        if (GPIO_Init->Mode & 0x00200000U) {
          EXTI->FTSR |= (1U << pinpos);
        }
        uint32_t cr_idx = pinpos >> 2;
        uint32_t cr_shift = (pinpos & 0x03) * 4;
        uint32_t port_code = 0;
        if (GPIOx == GPIOB) port_code = 1;
        else if (GPIOx == GPIOC) port_code = 2;
        else if (GPIOx == GPIOD) port_code = 3;
        AFIO->EXTICR[cr_idx] = (AFIO->EXTICR[cr_idx] & ~(0x0F << cr_shift)) | (port_code << cr_shift);
      }
    }
  }
}

void HAL_GPIO_DeInit(GPIO_TypeDef *GPIOx, uint32_t GPIO_Pin)
{
  (void)GPIOx; (void)GPIO_Pin;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
  return ((GPIOx->IDR & GPIO_Pin) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
  if (PinState != GPIO_PIN_RESET) {
    GPIOx->BSRR = GPIO_Pin;
  } else {
    GPIOx->BRR = GPIO_Pin;
  }
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
  GPIOx->ODR ^= GPIO_Pin;
}
