/* ARM semihosting (Thumb bkpt 0xAB) console + exit for QEMU. */
#include <stdint.h>

#define SYS_WRITE0 0x04
#define SYS_EXIT 0x18
#define ADP_Stopped_ApplicationExit 0x20026

static uintptr_t semihost_call(uintptr_t op, uintptr_t arg) {
    register uintptr_t r0 __asm__("r0") = op;
    register uintptr_t r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

void semihost_puts(const char *s) { semihost_call(SYS_WRITE0, (uintptr_t)s); }

void semihost_exit(int code) {
    /* QEMU exits 0 on ApplicationExit, 1 on anything else. */
    semihost_call(SYS_EXIT, code == 0 ? ADP_Stopped_ApplicationExit : 0);
    for (;;)
        ;
}

/* Unsigned decimal, no libc printf. */
void semihost_put_u32(uint32_t v) {
    char buf[11];
    char *p = buf + sizeof(buf) - 1;
    *p = '\0';
    do {
        *--p = '0' + (v % 10);
        v /= 10;
    } while (v);
    semihost_puts(p);
}
