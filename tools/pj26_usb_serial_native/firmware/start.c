typedef unsigned int u32;
extern u32 _estack, _sbss, _ebss;
void main_c(void);
void USART2_IRQHandler(void);
void Reset_Handler(void)
{
    for (u32 *p = &_sbss; p < &_ebss; p++) *p = 0;
    main_c();
    for (;;) {}
}
static void Default_Handler(void) { for (;;) {} }
__attribute__((section(".isr_vector"), used))
void (*const vectors[55])(void) = {
    [0] = (void (*)(void))&_estack, [1] = Reset_Handler,
    [2 ... 53] = Default_Handler,
    [54] = USART2_IRQHandler,
};
