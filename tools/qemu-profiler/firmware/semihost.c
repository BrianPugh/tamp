/* ARM semihosting (Thumb bkpt 0xAB) console + exit + file I/O for QEMU. */
#include <stdint.h>
#include <string.h>

#define SYS_OPEN 0x01
#define SYS_CLOSE 0x02
#define SYS_WRITE0 0x04
#define SYS_READ 0x06
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

/* Host-file access for corpus replay. Paths resolve against QEMU's cwd. */
int semihost_open_rb(const char *path) {
    uintptr_t args[3] = {(uintptr_t)path, 1 /* "rb" */, strlen(path)};
    return (int)semihost_call(SYS_OPEN, (uintptr_t)args);
}

int semihost_read(int handle, void *buf, uint32_t len) {
    uintptr_t args[3] = {(uintptr_t)handle, (uintptr_t)buf, len};
    /* SYS_READ returns the number of bytes NOT read. */
    uintptr_t not_read = semihost_call(SYS_READ, (uintptr_t)args);
    return (int)(len - not_read);
}

void semihost_close(int handle) {
    uintptr_t args[1] = {(uintptr_t)handle};
    semihost_call(SYS_CLOSE, (uintptr_t)args);
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
