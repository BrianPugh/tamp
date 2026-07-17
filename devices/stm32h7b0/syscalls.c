/* Newlib syscall stubs. stdout goes to the debugger via ARM semihosting
 * (SYS_WRITE, one trap per _write), so the harness needs only the ST-Link - no
 * UART wiring. The firmware must run under `make stm32h7b0-device-*` (OpenOCD
 * with semihosting enabled); standalone, the BKPT escalates to HardFault. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#define SEMIHOST_SYS_OPEN 0x01
#define SEMIHOST_SYS_WRITE 0x05

static uintptr_t semihost_call(uintptr_t op, uintptr_t arg) {
    register uintptr_t r0 __asm__("r0") = op;
    register uintptr_t r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt #0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

/* Lazily open the debugger console (":tt") in write mode ("w" == 4) once and
 * cache the handle; SYS_OPEN takes {path, mode, path_len} and returns a handle
 * (or -1 on failure). */
static int semihost_stdout(void) {
    static int handle = 0; /* 0 == not yet opened; semihosting handles are >0 */
    if (handle == 0) {
        static const char path[] = ":tt";
        uintptr_t args[3] = {(uintptr_t)path, 4, sizeof(path) - 1};
        handle = (int)semihost_call(SEMIHOST_SYS_OPEN, (uintptr_t)args);
    }
    return handle;
}

int _write(int fd, const char *buf, int len) {
    (void)fd;
    int handle = semihost_stdout();
    if (handle == -1) {
        errno = EIO;
        return -1;
    }
    /* SYS_WRITE takes {handle, buf, len} and returns the count NOT written. */
    uintptr_t args[3] = {(uintptr_t)handle, (uintptr_t)buf, (uintptr_t)len};
    int not_written = (int)semihost_call(SEMIHOST_SYS_WRITE, (uintptr_t)args);
    if (not_written == len) {
        errno = EIO;
        return -1;
    }
    return len - not_written;
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
