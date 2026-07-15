/* STM32H7B0VBT6: 128K internal flash, 128K DTCM, 1M AXI SRAM.
 * The first 256K of AXI SRAM (0x24000000) is reserved for the benchmark data
 * blob loaded over SWD by run.tcl; the linker must not place anything there. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
    DTCM (rw)   : ORIGIN = 0x20000000, LENGTH = 128K
    AXIRAM (rw) : ORIGIN = 0x24040000, LENGTH = 768K
}

_estack = ORIGIN(DTCM) + LENGTH(DTCM);

SECTIONS
{
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        *(.glue_7) *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > AXIRAM AT> FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > AXIRAM

    . = ALIGN(8);
    __heap_start = .;
    __heap_end = ORIGIN(AXIRAM) + LENGTH(AXIRAM);

    /DISCARD/ : { *(.note*) *(.comment) }
}
