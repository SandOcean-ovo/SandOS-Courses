; 阶段 0 独立启动：初始 MSP 位于 20 KiB RAM 顶端并按 8 字节对齐。
; RESET 段在 scatter 中放在最前，向量索引 14/15 对应 PendSV/SysTick。
; 不调用 __main，不依赖 C 库；probe.c 手动初始化全部观测变量。
        PRESERVE8
        THUMB
        AREA RESET, DATA, READONLY
        EXPORT __Vectors
        IMPORT probe_main
        IMPORT PendSV_Handler
        IMPORT SysTick_Handler
__Vectors
        DCD 0x20005000, Reset_Handler
        DCD Default_Handler, Default_Handler, Default_Handler, Default_Handler
        DCD Default_Handler, 0, 0, 0, 0, Default_Handler
        DCD Default_Handler, 0, PendSV_Handler, SysTick_Handler
        AREA |.text|, CODE, READONLY
Reset_Handler PROC
        EXPORT Reset_Handler
        B probe_main
        ENDP
; 未预期异常保留现场，运行器应超时失败，不能落到完成标记。
Default_Handler PROC
        B Default_Handler
        ENDP
; 返回 IPSR 的异常号：线程为 0，PendSV 为 14，SysTick 为 15。
read_ipsr PROC
        EXPORT read_ipsr
        MRS r0, IPSR
        BX lr
        ENDP
        END
