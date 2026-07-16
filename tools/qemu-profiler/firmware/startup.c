/* Minimal Cortex-M startup for the QEMU MPS2 profiling harness.
 * The ELF is loaded directly into emulated SRAM (VMA == LMA), so no .data
 * copy is needed; guest memory starts zeroed, but .bss is cleared anyway. */
#include <stdint.h>

extern uint32_t __bss_start__[];
extern uint32_t __bss_end__[];
extern uint32_t __stack_top__[];

int main(void);
void semihost_exit(int code);
void semihost_puts(const char *s);

void Reset_Handler(void) {
    for (uint32_t *p = __bss_start__; p < __bss_end__; p++) *p = 0;
    semihost_exit(main());
}

static void Fault_Handler(void) {
    semihost_puts("FAULT\n");
    semihost_exit(1);
}

__attribute__((section(".vectors"), used)) static void (*const vector_table[16])(void) = {
    (void (*)(void))__stack_top__, /* initial SP */
    Reset_Handler,                 /* Reset */
    Fault_Handler,                 /* NMI */
    Fault_Handler,                 /* HardFault */
    Fault_Handler,                 /* MemManage */
    Fault_Handler,                 /* BusFault */
    Fault_Handler,                 /* UsageFault */
    0,
    0,
    0,
    0,
    Fault_Handler, /* SVCall */
    0,
    0,
    Fault_Handler, /* PendSV */
    Fault_Handler, /* SysTick */
};
