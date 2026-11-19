#ifndef ADAU_PARAMS_H
#define ADAU_PARAMS_H

/* ADAU1401 / ADAU1701 parameter RAM addresses used by sound_app.c
 *
 * !! These MUST match your SigmaStudio project. !!
 * SigmaStudio assigns the addresses itself. After you build the signal chain, export the system
 * files (the generated *_IC_1_PARAM.h) and copy the real address of each cell into the defines
 * below. Parameter RAM is addressed in WORDS (one 32-bit parameter per address), so cells are
 * usually 1 apart, not 4 apart. Writing to addresses that do not belong to these cells changes
 * unrelated filters of whatever program is loaded.
 *
 * The values below are placeholders that keep the old behaviour of the code. */

#define ADAU_I2C_ADDR_8BIT      (0x34U << 1)   /* 0x68 write address, ADDR0/ADDR1 low */

#define ADAU_P_MASTER_VOL       0x0000U
#define ADAU_P_BASS             0x0004U
#define ADAU_P_MID              0x0008U
#define ADAU_P_TREBLE           0x000CU
#define ADAU_P_EQ_BAND0         0x0010U        /* 31 Hz; bands 1..4 follow at +ADAU_P_EQ_STRIDE */
#define ADAU_P_EQ_STRIDE        0x0004U
#define ADAU_P_GATE_EN          0x0024U
#define ADAU_P_COMP_EN          0x0028U
#define ADAU_P_REVERB_MIX       0x002CU
#define ADAU_P_WIDE_WIDTH       0x0030U
#define ADAU_P_SAT_DRIVE        0x0034U
#define ADAU_P_CROSS_EN         0x0038U

#endif
