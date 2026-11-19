/**
  ******************************************************************************
  * @file     core_cm3.h
  * @brief    CMSIS Cortex-M3 Core Peripheral Access Layer Header File
  ******************************************************************************
  */
#ifndef __CORE_CM3_H_GENERIC
#define __CORE_CM3_H_GENERIC

#include <stdint.h>

#ifdef __cplusplus
 extern "C" {
#endif

#define __I  volatile const
#define __O  volatile
#define __IO volatile

typedef struct {
  __IO uint32_t ISER[8U];
       uint32_t RESERVED0[24U];
  __IO uint32_t ICER[8U];
       uint32_t RSERVED1[24U];
  __IO uint32_t ISPR[8U];
       uint32_t RESERVED2[24U];
  __IO uint32_t ICPR[8U];
       uint32_t RESERVED3[24U];
  __IO uint32_t IABR[8U];
       uint32_t RESERVED4[56U];
  __IO uint8_t  IP[240U];
       uint32_t RESERVED5[644U];
  __O  uint32_t STIR;
}  NVIC_Type;

typedef struct {
  __IO uint32_t CPUID;
  __IO uint32_t ICSR;
  __IO uint32_t VTOR;
  __IO uint32_t AIRCR;
  __IO uint32_t SCR;
  __IO uint32_t CCR;
  __IO uint8_t  SHP[12U];
  __IO uint32_t SHCSR;
  __IO uint32_t CFSR;
  __IO uint32_t HFSR;
  __IO uint32_t DFSR;
  __IO uint32_t MMFAR;
  __IO uint32_t BFAR;
  __IO uint32_t AFSR;
} SCB_Type;

typedef struct {
  __IO uint32_t CTRL;
  __IO uint32_t LOAD;
  __IO uint32_t VAL;
  __I  uint32_t CALIB;
} SysTick_Type;

typedef struct {
  __IO uint32_t CTRL;
  __IO uint32_t CYCCNT;
  __IO uint32_t CPICNT;
  __IO uint32_t EXCCNT;
  __IO uint32_t SLEEPCNT;
  __IO uint32_t LSUCNT;
  __IO uint32_t FOLDCNT;
  __IO uint32_t PCSR;
} DWT_Type;

typedef struct {
  __IO uint32_t DHCSR;
  __O  uint32_t DCRSR;
  __IO uint32_t DCRDR;
  __IO uint32_t DEMCR;
} CoreDebug_Type;

#define SCS_BASE            (0xE000E000UL)
#define SysTick_BASE        (SCS_BASE +  0x0010UL)
#define NVIC_BASE           (SCS_BASE +  0x0100UL)
#define SCB_BASE            (SCS_BASE +  0x0D00UL)
#define DWT_BASE            (0xE0001000UL)
#define CoreDebug_BASE      (0xE000EDF0UL)

#define SCB                 ((SCB_Type       *)     SCB_BASE      )
#define SysTick             ((SysTick_Type   *)     SysTick_BASE  )
#define NVIC                ((NVIC_Type      *)     NVIC_BASE     )
#define DWT                 ((DWT_Type       *)     DWT_BASE      )
#define CoreDebug           ((CoreDebug_Type *)     CoreDebug_BASE)

#define DWT_CTRL_CYCCNTENA_Msk     (1UL << 0)
#define CoreDebug_DEMCR_TRCENA_Msk (1UL << 24)

static inline void __enable_irq(void)  { __asm volatile ("cpsie i" : : : "memory"); }
static inline void __disable_irq(void) { __asm volatile ("cpsid i" : : : "memory"); }
static inline void __NOP(void)         { __asm volatile ("nop"); }
static inline void __DSB(void)         { __asm volatile ("dsb 0xF":::"memory"); }
static inline void __ISB(void)         { __asm volatile ("isb 0xF":::"memory"); }
static inline void __DMB(void)         { __asm volatile ("dmb 0xF":::"memory"); }

#define __NVIC_PRIO_BITS 4U

static inline void NVIC_SetPriority(int32_t IRQn, uint32_t priority) {
  if (IRQn < 0) {
    SCB->SHP[(((uint32_t)IRQn) & 0xFUL)-4UL] = (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & (uint32_t)0xFFUL);
  } else {
    NVIC->IP[((uint32_t)IRQn)] = (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & (uint32_t)0xFFUL);
  }
}

static inline uint32_t NVIC_GetPriority(int32_t IRQn) {
  if (IRQn < 0) {
    return ((uint32_t)(SCB->SHP[(((uint32_t)IRQn) & 0xFUL)-4UL] >> (8U - __NVIC_PRIO_BITS)));
  } else {
    return ((uint32_t)(NVIC->IP[((uint32_t)IRQn)] >> (8U - __NVIC_PRIO_BITS)));
  }
}

static inline uint32_t NVIC_GetPriorityGrouping(void) {
  return ((uint32_t)((SCB->AIRCR & (7UL << 8U)) >> 8U));
}

static inline uint32_t NVIC_EncodePriority(uint32_t PriorityGroup, uint32_t PreemptPriority, uint32_t SubPriority) {
  uint32_t PriorityGroupTmp = (PriorityGroup & (uint32_t)0x07UL);
  uint32_t PreemptPriorityBits = ((7UL - PriorityGroupTmp) > (uint32_t)(__NVIC_PRIO_BITS)) ? (uint32_t)(__NVIC_PRIO_BITS) : (uint32_t)(7UL - PriorityGroupTmp);
  uint32_t SubPriorityBits = ((PriorityGroupTmp + (uint32_t)(__NVIC_PRIO_BITS)) < (uint32_t)7UL) ? (uint32_t)0UL : (uint32_t)((PriorityGroupTmp - 7UL) + (uint32_t)(__NVIC_PRIO_BITS));
  return (((PreemptPriority & (uint32_t)((1UL << (PreemptPriorityBits)) - 1UL)) << SubPriorityBits) |
          ((SubPriority & (uint32_t)((1UL << (SubPriorityBits)) - 1UL))));
}

#ifdef __cplusplus
}
#endif

#endif
