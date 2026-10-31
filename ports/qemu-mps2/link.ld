/* mps2-an385: 代码区 0x00000000 起，数据区 0x20000000 起（QEMU 默认各 4 MB，这里用较小的值） */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

_Min_Stack_Size = 0x1000;

SECTIONS
{
    .text : {
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        /* crti/crtn 把 _init、_fini 的前导和返回指令分别放在 .init/.fini 的开头与结尾，
           必须在同一个输出段内按输入顺序合并，否则 _init 没有 pop {..., pc} 会跑飞 */
        KEEP(*(.init))
        KEEP(*(.fini))
    } > FLASH

    .init_array : {
        . = ALIGN(4);
        __preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .;
        __init_array_start = .;    KEEP(*(.init_array*))    __init_array_end = .;
    } > FLASH

    .ARM.exidx : { *(.ARM.exidx*) } > FLASH

    _sidata = LOADADDR(.data);
    .data : {
        _sdata = .; *(.data*) . = ALIGN(4); _edata = .;
    } > RAM AT > FLASH

    .bss : {
        _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .;
    } > RAM

    /* 堆从 bss 末尾向上增长（newlib _sbrk 用 end），栈从 RAM 顶部向下 */
    PROVIDE(end = _ebss);
    PROVIDE(_end = _ebss);

    PROVIDE(_estack = ORIGIN(RAM) + LENGTH(RAM));
    /DISCARD/ : { *(.note*) }
}
