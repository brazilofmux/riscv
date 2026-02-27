/* Minimal linker script for RV32IM microcontroller binaries */
ENTRY(_start)

MEMORY
{
    CODE (rx)  : ORIGIN = 0x00010000, LENGTH = 1M
    DATA (rw)  : ORIGIN = 0x00080000, LENGTH = 16M
}

SECTIONS
{
    .text : {
        *(.text.init)
        *(.text .text.*)
    } > CODE

    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > CODE

    .data : {
        __data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        __data_end = .;
    } > DATA

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        __bss_end = .;
    } > DATA

    _end = .;
    __heap_start = .;
}
