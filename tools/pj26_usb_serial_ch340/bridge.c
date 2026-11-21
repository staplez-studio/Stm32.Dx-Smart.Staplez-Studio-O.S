/* PJ26 USB-serial adapter firmware (standalone, no libraries, no HAL).
 * DX-SMART PJ26 (STM32F103C8T6): CH340 USB-C  <->  USART1 (PA9 TX / PA10 RX)  <->  copied to  <->  USART2 (PA2 TX / PA3 RX).
 * The second pair is the 3.3 V serial port you wire to the ESP32-CAM.  Fixed 115200 8N1 both sides.
 * Clock: 8 MHz crystal x9 = 72 MHz (falls back to the internal 8 MHz RC if the crystal does not start).
 * Blue LED (PB11, active high) lights while bytes pass. */
typedef unsigned int u32;
typedef unsigned char u8;
#define REG(a) (*(volatile u32 *)(a))

#define RCC_CR       REG(0x40021000)
#define RCC_CFGR     REG(0x40021004)
#define RCC_APB2ENR  REG(0x40021018)
#define RCC_APB1ENR  REG(0x4002101C)
#define FLASH_ACR    REG(0x40022000)
#define GPIOA_CRL    REG(0x40010800)
#define GPIOA_CRH    REG(0x40010804)
#define GPIOA_ODR    REG(0x4001080C)
#define GPIOB_CRH    REG(0x40010C04)
#define GPIOB_BSRR   REG(0x40010C10)
#define U1_SR  REG(0x40013800)
#define U1_DR  REG(0x40013804)
#define U1_BRR REG(0x40013808)
#define U1_CR1 REG(0x4001380C)
#define U2_SR  REG(0x40004400)
#define U2_DR  REG(0x40004404)
#define U2_BRR REG(0x40004408)
#define U2_CR1 REG(0x4000440C)

#define SR_ORE (1u << 3)
#define SR_RXNE (1u << 5)
#define SR_TXE (1u << 7)

#define RING 512u                      /* power of two */
typedef struct { u8 b[RING]; u32 h, t; } ring_t;

void main_c(void)
{
    u32 pclk1 = 8000000u, pclk2 = 8000000u;

    /* ---- clock: HSE + PLL x9 = 72 MHz, APB1 /2, 2 flash wait states ---- */
    RCC_CR |= (1u << 16);                                   /* HSEON */
    u32 t = 200000u;
    while (!(RCC_CR & (1u << 17)) && --t) {}                /* HSERDY */
    if (RCC_CR & (1u << 17)) {
        FLASH_ACR = (1u << 4) | 2u;                         /* prefetch on, 2 wait states */
        RCC_CFGR = (1u << 16) | (7u << 18) | (4u << 8);     /* PLLSRC=HSE, MUL x9, PPRE1 = /2 */
        RCC_CR |= (1u << 24);                               /* PLLON */
        t = 200000u;
        while (!(RCC_CR & (1u << 25)) && --t) {}            /* PLLRDY */
        if (RCC_CR & (1u << 25)) {
            RCC_CFGR = (RCC_CFGR & ~3u) | 2u;               /* SW = PLL */
            t = 200000u;
            while (((RCC_CFGR >> 2) & 3u) != 2u && --t) {}
            if (((RCC_CFGR >> 2) & 3u) == 2u) { pclk1 = 36000000u; pclk2 = 72000000u; }
        }
    }
    if (pclk2 == 8000000u) {                                /* no PLL: run from whatever is selected, 8 MHz */
        RCC_CFGR &= ~(7u << 8);                             /* APB1 /1 */
        RCC_CFGR = (RCC_CFGR & ~3u);                        /* SW = HSI */
        RCC_CR &= ~(1u << 24);
    }

    /* ---- pins ---- */
    RCC_APB2ENR |= (1u << 2) | (1u << 3) | (1u << 14);      /* GPIOA, GPIOB, USART1 */
    RCC_APB1ENR |= (1u << 17);                              /* USART2 */
    GPIOA_ODR |= (1u << 3) | (1u << 10);                    /* pull-ups on the two RX pins */
    GPIOA_CRL = (GPIOA_CRL & ~(0xFFu << 8)) | (0xBu << 8) | (0x8u << 12);        /* PA2 AF push-pull, PA3 input pull */
    GPIOA_CRH = (GPIOA_CRH & ~(0xFFu << 4)) | (0xBu << 4) | (0x8u << 8);         /* PA9 AF push-pull, PA10 input pull */
    GPIOB_CRH = (GPIOB_CRH & ~(0xFu << 12)) | (0x2u << 12);                      /* PB11 output (LED) */

    U1_BRR = (pclk2 + 57600u) / 115200u;
    U2_BRR = (pclk1 + 57600u) / 115200u;
    U1_CR1 = (1u << 13) | (1u << 3) | (1u << 2);            /* UE, TE, RE */
    U2_CR1 = (1u << 13) | (1u << 3) | (1u << 2);

    static ring_t to_cam, to_pc;                            /* zero-initialised by Reset (see start.c) */
    u32 led = 0;

    for (;;) {
        u32 s1 = U1_SR, s2 = U2_SR;
        if (s1 & (SR_RXNE | SR_ORE)) {                      /* PC -> cam */
            u8 b = (u8)U1_DR;                               /* reading SR then DR clears the flags */
            if (to_cam.h - to_cam.t < RING) { to_cam.b[to_cam.h++ & (RING - 1u)] = b; led = 300000u; }
        }
        if (s2 & (SR_RXNE | SR_ORE)) {                      /* cam -> PC */
            u8 b = (u8)U2_DR;
            if (to_pc.h - to_pc.t < RING) { to_pc.b[to_pc.h++ & (RING - 1u)] = b; led = 300000u; }
        }
        if (to_cam.h != to_cam.t && (s2 & SR_TXE)) U2_DR = to_cam.b[to_cam.t++ & (RING - 1u)];
        if (to_pc.h != to_pc.t && (s1 & SR_TXE))   U1_DR = to_pc.b[to_pc.t++ & (RING - 1u)];
        GPIOB_BSRR = led ? (1u << 11) : (1u << (11 + 16));
        if (led) led--;
    }
}
