ENTRY(_start)

SECTIONS
{
    . = 1M;

    /* Multiboot PRIMEIRO (12 bytes em 0x100000).
       Depois ALIGN(16) -> _start em 0x100010. */
    .text : {
        KEEP(*(.multiboot))
        . = ALIGN(16);
        *(.text.entry)
        *(.text)
        *(.text.*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data) }

    .bss : {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    }

    . = ALIGN(16);
    . += 0x40000;
    __stack_top = .;
}
