/**
  * @file    level_meter.h
  * @brief   Real stereo level meter: the STM32's own 12 bit ADC measures the audio on PA4 (left) and PA5 (right).
  *          (The PCM1808 / PCM5102 need I2S, which the STM32F103 does not have, so they cannot be used.)
  *
  *          Wire the audio you want to watch (line input, or the DSP output) like this, per channel:
  *              audio ──┤├── 10 µF ──┬── 1 kΩ ──── PA4 (left) / PA5 (right)
  *                                   ├── 10 kΩ to 3V3
  *                                   └── 10 kΩ to GND          (bias: centres the signal at 1.65 V)
  *          Keep the signal below 3.3 V peak-to-peak (line level 2 Vrms is too much: divide it down).
  *          The meter measures peak-to-peak swing over a ~2 ms window, so DC bias does not matter.
  */
#ifndef LEVEL_METER_H
#define LEVEL_METER_H

#include <stdint.h>
#include <stdbool.h>

#define LEVEL_SEGMENTS 20            /* bar length; 3 dB per segment, top segment = full scale (3.3 Vpp) */

typedef struct {
    uint8_t  seg[2];                 /* 0..20 current level, [0] = left (PA4), [1] = right (PA5) */
    uint8_t  peak[2];                /* peak-hold marker, falls slowly */
    uint16_t p2p[2];                 /* raw peak-to-peak, ADC counts (0..4095) */
    bool     adc_ok;                 /* false = the ADC did not answer */
} level_t;

void LevelMeter_Init(void);          /* analog pins + ADC1 + calibration (call once, cheap if repeated) */
void LevelMeter_Update(void);        /* take one measurement (about 3 ms) and update the bars */
const level_t *LevelMeter_Get(void);
int  LevelMeter_Db(uint8_t seg);     /* dB below full scale for a segment (0, -3, -6 ...), -99 for none */

/* Replace the ADC read (unit tests on a PC only; leave NULL on the real board). */
void LevelMeter_SetSampleHook(uint16_t (*fn)(uint8_t channel));

/* Pure function: bar length for a peak-to-peak value (exported for tests) */
uint8_t LevelMeter_SegmentsFor(uint16_t p2p);

#endif
