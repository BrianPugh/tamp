/* Newlib syscall stubs. stdout goes to the debugger via ARM semihosting
 * (SYS_WRITE0), so the harness needs only the ST-Link - no UART wiring. The
 * firmware must run under `make stm32h7b0-device-*` (OpenOCD with semihosting
 * enabled); standalone, the BKPT escalates to HardFault. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#define SEMIHOST_SYS_WRITE0 0x04

static uintptr_t semihost_call(uintptr_t op, uintptr_t arg) {
    register uintptr_t r0 __asm__("r0") = op;
    register uintptr_t r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt #0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

int _write(int fd, const char *buf, int len) {
    (void)fd;
    char chunk[64];
    int remaining = len;
    while (remaining > 0) {
        int n = remaining < (int)sizeof(chunk) - 1 ? remaining : (int)sizeof(chunk) - 1;
        for (int i = 0; i < n; i++) chunk[i] = *buf++;
        chunk[n] = '\0';
        semihost_call(SEMIHOST_SYS_WRITE0, (uintptr_t)chunk);
        remaining -= n;
    }
    return len;
}

/* Heap bounds come from the linker script (AXI SRAM); the default newlib
 * heuristic of "heap ends at the stack pointer" is wrong here because the
 * stack lives in DTCM, numerically below the heap. */
extern uint8_t __heap_start, __heap_end;

void *_sbrk(ptrdiff_t incr) {
    static uint8_t *brk = &__heap_start;
    if (brk + incr > &__heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    uint8_t *prev = brk;
    brk += incr;
    return prev;
}

int _close(int fd) {
    (void)fd;
    return -1;
}

int _fstat(int fd, struct stat *st) {
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd) {
    (void)fd;
    return 1;
}

off_t _lseek(int fd, off_t offset, int whence) {
    (void)fd;
    (void)offset;
    (void)whence;
    return 0;
}

int _read(int fd, char *buf, int len) {
    (void)fd;
    (void)buf;
    (void)len;
    return 0;
}

int _getpid(void) { return 1; }

int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status) {
    (void)status;
    for (;;)
        ;
}
