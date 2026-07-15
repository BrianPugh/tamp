/* Minimal bare-metal startup: vector table, FPU enable, .data/.bss init. */
#include <stdint.h>

#include "stm32h7xx.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

extern int main(void);
void tamp_system_init(void);

void Reset_Handler(void) {
    /* Enable FPU (CP10/CP11 full access) before any code that may touch FP
     * registers; the whole build uses -mfloat-abi=hard. */
    SCB->CPACR |= (0xFUL << 20);
    __DSB();
    __ISB();

    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata;) *dst++ = *src++;
    for (uint32_t *dst = &_sbss; dst < &_ebss;) *dst++ = 0;

    tamp_system_init();
    main();
    for (;;)
        ;
}

void Default_Handler(void) {
    for (;;)
        ;
}

#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_DEFAULT;
void HardFault_Handler(void) WEAK_DEFAULT;
void MemManage_Handler(void) WEAK_DEFAULT;
void BusFault_Handler(void) WEAK_DEFAULT;
void UsageFault_Handler(void) WEAK_DEFAULT;
void SVC_Handler(void) WEAK_DEFAULT;
void DebugMon_Handler(void) WEAK_DEFAULT;
void PendSV_Handler(void) WEAK_DEFAULT;
void SysTick_Handler(void) WEAK_DEFAULT;

/* No peripheral interrupts are enabled; only the core exception slots are
 * populated. */
__attribute__((section(".isr_vector"), used)) static void (*const vector_table[])(void) = {
    (void (*)(void))(&_estack),
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0,
    0,
    0,
    0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
};
