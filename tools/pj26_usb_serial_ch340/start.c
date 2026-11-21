/* Minimal Cortex-M3 start-up: vector table + reset handler (clears .bss, no C library). */
typedef unsigned int u32;
extern u32 _estack, _sbss, _ebss;
void main_c(void);

void Reset_Handler(void)
{
    for (u32 *p = &_sbss; p < &_ebss; p++) *p = 0;
    main_c();
    for (;;) {}
}
static void Default_Handler(void) { for (;;) {} }

__attribute__((section(".isr_vector"), used))
void (*const vectors[16])(void) = {
    (void (*)(void))&_estack, Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, Default_Handler, 0, 0, 0, 0,
    Default_Handler, Default_Handler, 0, Default_Handler, Default_Handler,
};
