/* MPS2 (an385/an386/an500): code+rodata in ZBT SSRAM1 @ 0x0,
 * data/bss/stack in SSRAM2/3 @ 0x20000000. ELF is loaded directly by QEMU,
 * so VMA == LMA everywhere and no startup copy is required. */
MEMORY
{
    CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    DATA (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
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
