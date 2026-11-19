/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.c
  * @brief   TIM1 PWM Hardware Generator for 120mm Computer Fan on PA8.
  *          Frequency: 25 kHz standard PC 4-pin PWM curve.
  ******************************************************************************
  */
/* USER CODE END Header */
#include "tim.h"

TIM_HandleTypeDef htim1;

void MX_TIM1_Init(void)
{
  TIM_OC_InitTypeDef sConfigOC = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM1_CLK_ENABLE();

  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 2; // 72MHz / (2 + 1) = 24MHz
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 959; // 24MHz / 960 = 25.0 kHz
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  HAL_TIM_PWM_Init(&htim1);

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 480; // 50% default duty
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
}

void Fan_Set_PWM(uint8_t duty_percent)
{
  if (duty_percent > 100) duty_percent = 100;
  /* Counter runs 0..959 (960 steps). 100% needs CCR > ARR so the pin stays high; the old
   * scale-by-959 left a 1-step low glitch every period at "100%". Integer maths, no soft-float. */
  uint32_t compare_val = ((uint32_t)duty_percent * 960U) / 100U;
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, compare_val);
}
