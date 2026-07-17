/* MPS2 AN505 (Cortex-M33 IoTKit): boots in Secure state; code in SSRAM1 via
 * its secure alias @ 0x10000000, data/bss/stack in SSRAM2/3 secure alias
 * @ 0x38000000. Same section layout as link.ld otherwise. */
MEMORY
{
    CODE (rx)  : ORIGIN = 0x10000000, LENGTH = 4M
    DATA (rwx) : ORIGIN = 0x38000000, LENGTH = 4M
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > CODE

    .data :
    {
        *(.data*)
        . = ALIGN(4);
    } > DATA

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > DATA

    __stack_top__ = ORIGIN(DATA) + LENGTH(DATA);

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
