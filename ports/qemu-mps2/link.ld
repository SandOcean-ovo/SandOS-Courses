/*
 * link.ld —— QEMU mps2-an385 链接脚本
 *
 * 内存布局：
 *   0x00000000  FLASH  代码与只读数据（QEMU 把 ELF 的 LOAD 段直接装到这里）
 *   0x20000000  RAM    .data / .bss / 堆 / 栈
 *
 * mps2-an385 实际的 ZBT SSRAM 比这里写的大（各 4 MB），这里取较小的上限，
 * 用于更早发现“程序比预期大”的问题。
 */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

SECTIONS
{
    /* 代码段：向量表必须在最前面，因为 CPU 复位后从地址 0 读取它 */
    .text : {
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);

        /*
         * crti.o / crtn.o 把 _init 的前半段（push 指令）放在 .init 的开头，
         * 把后半段（pop {..., pc} 返回）放在 .init 的结尾，两个文件各贡献一部分。
         * 因此 .init / .fini 必须在同一个输出段内按输入顺序合并；
         * 如果单独放成别的段，_init 会缺少返回指令，__libc_init_array 调用后跑飞，
         * 现象是 QEMU 完全没有串口输出（本工程阶段 1 实际遇到过，见 docs 的排错记录）。
         */
        KEEP(*(.init))
        KEEP(*(.fini))
    } > FLASH

    /* C 库用于执行启动期初始化函数的数组，由 __libc_init_array 遍历 */
    .init_array : {
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP(*(.preinit_array*))
        __preinit_array_end = .;
        __init_array_start = .;
        KEEP(*(.init_array*))
        __init_array_end = .;
    } > FLASH

    .ARM.exidx : { *(.ARM.exidx*) } > FLASH

    /* .data：运行时在 RAM，初值存放在 FLASH（LMA），由 Reset_Handler 拷贝 */
    _sidata = LOADADDR(.data);
    .data : {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /* .bss：只占 RAM，启动时清零 */
    .bss : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /*
     * 堆从 .bss 末尾向上增长（newlib 的 _sbrk 使用符号 end），
     * 初始 MSP 取 RAM 顶端并向下增长。本工程任务使用各自的静态栈，
     * MSP 只用于启动阶段和中断/异常处理。
     */
    PROVIDE(end  = _ebss);
    PROVIDE(_end = _ebss);
    PROVIDE(_estack = ORIGIN(RAM) + LENGTH(RAM));

    /DISCARD/ : { *(.note*) }
}
