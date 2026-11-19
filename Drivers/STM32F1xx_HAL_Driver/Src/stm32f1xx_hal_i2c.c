/**
  ******************************************************************************
  * @file    stm32f1xx_hal_i2c.c
  * @brief   Robust, high-speed hardware & open-drain I2C driver for STM32F103.
  *          Features automated 9-clock bus unfreeze recovery and internal pull-ups
  *          on PB8 (SCL) and PB9 (SDA), fully driving SH1106/SSD1306 OLEDs and DS3231.
  ******************************************************************************
  */
#include "stm32f1xx_hal.h"

#define I2C_SCL_PIN   GPIO_PIN_8
#define I2C_SDA_PIN   GPIO_PIN_9
#define I2C_PORT      GPIOB

/* Bus timing.
 * The old delay was two NOPs, so the real SCL rate depended on the compiler's optimisation level
 * (roughly ~1 MHz at -O0 and several MHz at -Os), well above the 400 kHz limit of the SH1106,
 * DS3231 and AT24C32. The delay is now timed with the DWT cycle counter instead.
 *
 * One "unit" is the shortest SCL phase. Every byte bit uses 3 units (low 2 / high 1), so
 *   SCL frequency = I2C_BITBANG_HZ,  high >= 1 unit,  low >= 2 units.
 * At 300 kHz / 72 MHz that is high ~1.1 us, low ~2.2 us (fast-mode minimums: 0.6 / 1.3 us).
 * Lower I2C_BITBANG_HZ (e.g. 100000U) if you use long wires or weak pull-ups. */
#ifndef I2C_BITBANG_HZ
#define I2C_BITBANG_HZ 300000U
#endif

static uint32_t i2c_unit_cycles = 80U;  /* recomputed in HAL_I2C_Init from SystemCoreClock */
static uint8_t  i2c_dwt_ok      = 0U;

static inline void i2c_delay(void)
{
  if (i2c_dwt_ok) {
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < i2c_unit_cycles) { }
  } else {
    /* Fallback if the cycle counter is not running: deliberately slow (>= ~100 kHz). */
    for (volatile uint32_t i = 0; i < i2c_unit_cycles; i++) { __NOP(); }
  }
}

static inline void scl_high(void)
{
  I2C_PORT->BSRR = I2C_SCL_PIN;
}

static inline void scl_low(void)
{
  I2C_PORT->BRR = I2C_SCL_PIN;
}

static inline void sda_high(void)
{
  I2C_PORT->BSRR = I2C_SDA_PIN;
}

static inline void sda_low(void)
{
  I2C_PORT->BRR = I2C_SDA_PIN;
}

static inline uint8_t sda_read(void)
{
  return (I2C_PORT->IDR & I2C_SDA_PIN) ? 1 : 0;
}

static void i2c_bus_start(void)
{
  sda_high();
  i2c_delay();   /* repeated START: let SDA settle high while SCL is still low, otherwise a slowly
                  * rising SDA can overlap SCL going high and look like a STOP to the slave */
  scl_high();
  i2c_delay();
  sda_low();
  i2c_delay();
  scl_low();
  i2c_delay();
}

static void i2c_bus_stop(void)
{
  sda_low();
  i2c_delay();
  scl_high();
  i2c_delay();
  sda_high();
  i2c_delay();
}

static uint8_t i2c_write_byte(uint8_t byte)
{
  for (uint8_t i = 0; i < 8; i++) {
    if (byte & 0x80) {
      sda_high();
    } else {
      sda_low();
    }
    byte <<= 1;
    i2c_delay();
    scl_high();
    i2c_delay();
    scl_low();
    i2c_delay();
  }
  /* Clock 9th bit for ACK */
  sda_high();
  i2c_delay();
  scl_high();
  i2c_delay();
  uint8_t ack = (sda_read() == 0) ? 1 : 0;
  scl_low();
  i2c_delay();
  return ack;
}

static uint8_t i2c_read_byte(uint8_t send_ack)
{
  uint8_t byte = 0;
  sda_high();
  for (uint8_t i = 0; i < 8; i++) {
    byte <<= 1;
    i2c_delay();          /* SCL low phase = 2 units (this + the one after scl_low) */
    scl_high();
    i2c_delay();
    if (sda_read()) {
      byte |= 1;
    }
    scl_low();
    i2c_delay();
  }
  if (send_ack) {
    sda_low();
  } else {
    sda_high();
  }
  i2c_delay();
  scl_high();
  i2c_delay();
  scl_low();
  sda_high();
  i2c_delay();
  return byte;
}

static void i2c_bus_reset(void)
{
  sda_high();
  for (int i = 0; i < 9; i++) {
    scl_low();
    i2c_delay();
    scl_high();
    i2c_delay();
  }
  i2c_bus_stop();
}

HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hi2c)
{
  (void)hi2c;

  /* Make sure the DWT cycle counter runs, and verify it, before timing the bus with it */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  {
    uint32_t c0 = DWT->CYCCNT;
    for (volatile uint32_t k = 0; k < 16U; k++) { __NOP(); }
    i2c_dwt_ok = (DWT->CYCCNT != c0) ? 1U : 0U;
  }
  {
    uint32_t unit = SystemCoreClock / (3U * I2C_BITBANG_HZ);
    i2c_unit_cycles = (unit < 8U) ? 8U : unit;
  }

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_I2C1_CLK_ENABLE();
  __HAL_AFIO_REMAP_I2C1_ENABLE();

  /* Configure PB8 (SCL) as Push-Pull output for sharp clock transitions */
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = I2C_SCL_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(I2C_PORT, &GPIO_InitStruct);

  /* Configure PB9 (SDA) as Open-Drain output with internal Pull-up */
  GPIO_InitStruct.Pin = I2C_SDA_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(I2C_PORT, &GPIO_InitStruct);

  scl_high();
  sda_high();

  i2c_bus_reset();
  return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *hi2c)
{
  (void)hi2c;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint32_t Trials, uint32_t Timeout)
{
  (void)hi2c; (void)Timeout;
  while (Trials--) {
    i2c_bus_start();
    uint8_t ack = i2c_write_byte((uint8_t)(DevAddress & 0xFE));
    i2c_bus_stop();
    if (ack) {
      return HAL_OK;
    }
    HAL_Delay(1);
  }
  return HAL_ERROR;
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
  (void)hi2c; (void)Timeout;
  i2c_bus_start();
  if (!i2c_write_byte((uint8_t)(DevAddress & 0xFE))) {
    i2c_bus_stop();
    return HAL_ERROR;
  }
  for (uint16_t i = 0; i < Size; i++) {
    i2c_write_byte(pData[i]);
  }
  i2c_bus_stop();
  return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
  (void)hi2c; (void)Timeout;
  i2c_bus_start();
  if (!i2c_write_byte((uint8_t)(DevAddress | 0x01))) {
    i2c_bus_stop();
    return HAL_ERROR;
  }
  for (uint16_t i = 0; i < Size; i++) {
    pData[i] = i2c_read_byte((i < (Size - 1)) ? 1 : 0);
  }
  i2c_bus_stop();
  return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
  (void)hi2c; (void)Timeout;
  i2c_bus_start();
  if (!i2c_write_byte((uint8_t)(DevAddress & 0xFE))) {
    i2c_bus_stop();
    return HAL_ERROR;
  }
  if (MemAddSize == 2) {
    i2c_write_byte((uint8_t)(MemAddress >> 8));
    i2c_write_byte((uint8_t)(MemAddress & 0xFF));
  } else {
    i2c_write_byte((uint8_t)(MemAddress & 0xFF));
  }
  for (uint16_t i = 0; i < Size; i++) {
    i2c_write_byte(pData[i]);
  }
  i2c_bus_stop();
  return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
  (void)hi2c; (void)Timeout;
  i2c_bus_start();
  if (!i2c_write_byte((uint8_t)(DevAddress & 0xFE))) {
    i2c_bus_stop();
    return HAL_ERROR;
  }
  if (MemAddSize == 2) {
    i2c_write_byte((uint8_t)(MemAddress >> 8));
    i2c_write_byte((uint8_t)(MemAddress & 0xFF));
  } else {
    i2c_write_byte((uint8_t)(MemAddress & 0xFF));
  }
  i2c_bus_start();
  if (!i2c_write_byte((uint8_t)(DevAddress | 0x01))) {
    i2c_bus_stop();
    return HAL_ERROR;
  }
  for (uint16_t i = 0; i < Size; i++) {
    pData[i] = i2c_read_byte((i < (Size - 1)) ? 1 : 0);
  }
  i2c_bus_stop();
  return HAL_OK;
}
