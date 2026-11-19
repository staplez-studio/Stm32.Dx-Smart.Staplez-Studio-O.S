#include "level_meter.h"
#include "main.h"
#include <string.h>

/* ADC1 registers (STM32F103), defined here because the trimmed CMSIS header of this project has no ADC section */
typedef struct {
  volatile uint32_t SR, CR1, CR2, SMPR1, SMPR2, JOFR1, JOFR2, JOFR3, JOFR4, HTR, LTR, SQR1, SQR2, SQR3, JSQR, JDR1, JDR2, JDR3, JDR4, DR;
} adc_regs_t;
#define ADC1_REGS      ((adc_regs_t *)(APB2PERIPH_BASE + 0x2400UL))
#define ADC_CR2_ADON_  (1UL << 0)
#define ADC_CR2_CAL_   (1UL << 2)
#define ADC_CR2_RSTCAL_ (1UL << 3)
#define ADC_CR2_EXTTRIG_ (1UL << 20)
#define ADC_CR2_SWSTART_ (1UL << 22)
#define ADC_SR_EOC_    (1UL << 1)

#define SAMPLES        256U          /* per channel and measurement */
#define NOISE_FLOOR    8U            /* counts: below this the input is treated as silent */

static const uint16_t SEG_MIN_P2P[LEVEL_SEGMENTS] = { 8, 10, 12, 16, 23, 33, 46, 65, 92, 129, 183, 258, 365, 516, 728, 1029, 1453, 2052, 2899, 3700 };

static level_t lv;
static bool inited = false;
static uint16_t (*hook)(uint8_t) = NULL;

void LevelMeter_SetSampleHook(uint16_t (*fn)(uint8_t channel)) { hook = fn; }

static bool adc_wait(uint32_t bit)
{
    for (uint32_t n = 0; n < 200000UL; n++) if (!(ADC1_REGS->CR2 & bit)) return true;
    return false;
}

void LevelMeter_Init(void)
{
    memset(&lv, 0, sizeof lv);
    lv.adc_ok = true;
    if (hook) { inited = true; return; }

    RCC->APB2ENR |= (1UL << 2) | (1UL << 9);                    /* GPIOA + ADC1 clocks */
    RCC->CFGR = (RCC->CFGR & ~(3UL << 14)) | (2UL << 14);       /* ADC clock = PCLK2 / 6 = 12 MHz (limit 14 MHz) */
    GPIOA->CRL &= ~((0xFUL << 16) | (0xFUL << 20));             /* PA4, PA5: analog input (MODE=00, CNF=00) */

    ADC1_REGS->CR1 = 0;
    ADC1_REGS->CR2 = ADC_CR2_ADON_;                             /* wake up */
    for (volatile int i = 0; i < 2000; i++) { }                 /* >= 1 us stabilisation */
    ADC1_REGS->SMPR2 = (4UL << 12) | (4UL << 15);               /* channels 4 and 5: 41.5 cycles (fine for a 1 k source) */
    ADC1_REGS->SQR1 = 0;                                        /* one conversion per sequence */
    ADC1_REGS->CR2 |= ADC_CR2_RSTCAL_;
    lv.adc_ok = adc_wait(ADC_CR2_RSTCAL_);
    ADC1_REGS->CR2 |= ADC_CR2_CAL_;
    lv.adc_ok = lv.adc_ok && adc_wait(ADC_CR2_CAL_);
    ADC1_REGS->CR2 |= ADC_CR2_EXTTRIG_ | (7UL << 17);           /* software trigger */
    inited = true;
}

static uint16_t sample(uint8_t ch)
{
    if (hook) return hook(ch);
    ADC1_REGS->SQR3 = (uint32_t)(4U + ch);                      /* channel 4 = PA4, 5 = PA5 */
    ADC1_REGS->CR2 |= ADC_CR2_SWSTART_;
    for (uint32_t n = 0; n < 5000UL; n++) {
        if (ADC1_REGS->SR & ADC_SR_EOC_) return (uint16_t)(ADC1_REGS->DR & 0x0FFFU);
    }
    lv.adc_ok = false;
    return 0;
}

uint8_t LevelMeter_SegmentsFor(uint16_t p2p)
{
    if (p2p < NOISE_FLOOR) return 0;
    uint8_t s = 0;
    while (s < LEVEL_SEGMENTS && p2p >= SEG_MIN_P2P[s]) s++;
    return s;
}

int LevelMeter_Db(uint8_t seg)
{
    if (seg == 0) return -99;
    return -3 * (int)(LEVEL_SEGMENTS - seg);
}

void LevelMeter_Update(void)
{
    if (!inited) LevelMeter_Init();
    lv.adc_ok = true;
    for (uint8_t ch = 0; ch < 2; ch++) {
        uint16_t mn = 4095U, mx = 0U;
        for (uint16_t i = 0; i < SAMPLES; i++) {
            uint16_t v = sample(ch);
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        lv.p2p[ch] = (mx >= mn) ? (uint16_t)(mx - mn) : 0U;
        uint8_t s = LevelMeter_SegmentsFor(lv.p2p[ch]);
        /* instant attack, steady fall (about 1 segment per update) so the bar is readable */
        if (s >= lv.seg[ch]) lv.seg[ch] = s; else if (lv.seg[ch] > 0) lv.seg[ch]--;
        if (s >= lv.peak[ch]) lv.peak[ch] = s; else if (lv.peak[ch] > 0 && (HAL_GetTick() & 0x3F) == 0) lv.peak[ch]--;
    }
}

const level_t *LevelMeter_Get(void) { return &lv; }
