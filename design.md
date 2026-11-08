# SandOS 设计原理

本文档详细解析 SandOS 的核心设计原理，包括调度策略、上下文切换机制、同步机制以及内存管理策略。

## 1. 调度策略

SandOS 采用**基于优先级的抢占式调度** 策略。

### 1.1 优先级位图算法

为了实现 O(1) 复杂度的任务查找，RTOS 使用了位图算法来快速定位当前最高优先级的就绪任务。

*   **位图定义**: 系统维护一个全局变量 `g_PrioMap` (uint32_t)，每一位 (Bit) 对应一个优先级。
    *   `1` 表示该优先级下有就绪任务。
    *   `0` 表示该优先级下无就绪任务。
    *   支持 `OS_MAX_PRIO` (32) 个优先级，0 为最高优先级，31 为最低优先级。

*   **查找最高优先级**:
    调度器通过查找 `g_PrioMap` 中第一个置为 `1` 的位来确定最高优先级。这通常可以通过硬件指令（如 `clz` - Count Leading Zeros，或通过查表法）高效实现。

    > 在本 RTOS 实现中，为了通用性，采用了一个 256 字节的查找表 `OS_MapTable` 来加速 8 位数据的最高位查找。

*   **调度流程**:
    1.  **入队**: 当任务变为就绪态时，将其插入对应优先级就绪链表的尾部，并将 `g_PrioMap` 对应位置 1。
    2.  **出队**: 当任务阻塞或挂起时，从就绪链表中移除。如果该优先级链表为空，则将 `g_PrioMap` 对应位置 0。
    3.  **查找**: 调度器每次运行时，计算 `TopPrio = OS_GetTopPrio(g_PrioMap)`，然后从 `ReadyList[TopPrio]` 的头部获取下一个要运行的任务 `NextTCB`。

---

## 2. 上下文切换

上下文切换是 RTOS 的心脏，负责保存当前任务的状态并恢复下一个任务的状态。SandOS 针对 RISC-V (CH32V203) 架构进行了深度优化。

### 2.1 任务栈帧结构

每个任务拥有独立的栈空间。在发生切换时，CPU 寄存器被压入栈中。

**RISC-V (RV32I) 栈帧结构:**
```
High Address  +------------------+
              |      mstatus     | Offset 124
              +------------------+
              |       mepc       | Offset 120
              +------------------+
              |       x1 (ra)    | Offset 116
              +------------------+
              |       x3 (gp)    | Offset 112
              +------------------+
              |       x10 (a0)   | Offset 108
              +------------------+
              |       ...        |
              |   x5-x9, x11-x31 | General Purpose Registers
              |       ...        |
Low Address   +------------------+ <-- stackPtr (SP)
```

### 2.2 切换过程可视化

为了对比说明，以下展示了 Cortex-M3 (PenSV) 与 RISC-V (SWI) 上下文切换的异同。

#### 场景：Cortex-M3 (ARMv7-M)

Cortex-M3 支持硬件自动压栈 (xPSR, PC, LR, R12, R3-R0)，软件只需保存 R4-R11。

```ascii
   Task A (Running)           PendSV Handler           Task B (Next)
       |                            |                        |
       | [Trigger PendSV]           |                        |
       |--------------------------->|                        |
       | (HW: Push xPSR..R0)        |                        |
       |                            |                        |
       |                            | [Save Context A]       |
       |                            | PUSH {R4-R11}          |
       |                            | PSP -> TCB_A->Stack    |
       |                            |                        |
       |                            | [Select Task B]        |
       |                            | Current = Next         |
       |                            |                        |
       |                            | [Restore Context B]    |
       |                            | TCB_B->Stack -> PSP    |
       |                            | POP {R4-R11}           |
       |                            |                        |
       |                            | (HW: Pop xPSR..R0)     |
       |<---------------------------|                        |
                                    |                        |
                                    |----------------------->|
                                                         (Running)
```

#### 场景：RISC-V (CH32V203 - QingkeV4)

RISC-V 通常需要软件全手动保存上下文（除非硬件扩展支持）。SandOS 使用软件中断触发切换。

```ascii
   Task A (Running)            SW Handler (Trap)           Task B (Next)
       |                            |                          |
       | [Trigger SWI/ECALL]        |                          |
       |--------------------------->|                          |
       |                            | [Save Context A]         |
       |                            | addi sp, sp, -128        |
       |                            | sw x1, x3-x31 -> Stack   |
       |                            | csrr mepc, mstatus       |
       |                            | sw mepc, mstatus -> Stack|
       |                            | sp -> TCB_A->Stack       |
       |                            |                          |
       |                            | [Select Task B]          |
       |                            | Current = Next           |
       |                            |                          |
       |                            | [Restore Context B]      |
       |                            | TCB_B->Stack -> sp       |
       |                            | lw mepc, mstatus <- Stack|
       |                            | csrw mepc, mstatus       |
       |                            | lw x1, x3-x31 <- Stack   |
       |                            | addi sp, sp, 128         |
       |                            |                          |
       |                            | mret                     |
       |<---------------------------|                          |
                                    |                          |
                                    |------------------------->|
                                                           (Running)
```

**关键点:**
1.  **全寄存器保存**: RISC-V 需要手动保存几乎所有通用寄存器 (x1, x3-x31)。x0 (zero) 恒为 0 无需保存，x2 (sp) 是栈指针本身。
2.  **CSR 处理**: 必须保存 `mepc` (返回地址) 和 `mstatus` (中断状态)，确保任务恢复后能回到正确位置且中断状态正确。

---

## 3. 互斥锁与优先级继承

课程 M1 的互斥锁只支持单锁直接优先级继承，不承诺多锁或传递继承。每个任务用 `HeldMutex` 记录至多一把不同锁；同锁可递归 1～255 层，超过返回 `OS_ERR_NESTING`。申请第二把不同锁立即返回 `OS_ERR_RESOURCE`，不进入等待、不继承。最终释放清除旧持锁记录，交接时同时设置新所有权与 `TASK_READY`。

### 问题场景
1.  **低优先级任务 (L)** 持有锁 M。
2.  **高优先级任务 (H)** 抢占了 L，并试图申请锁 M，结果被阻塞。
3.  **中优先级任务 (M)** 抢占了 L。
4.  **结果**: H 被 M 间接阻塞了，这破坏了实时性。

### 解决方案：优先级继承
当高优先级任务 H 申请一个已经被低优先级任务 L 持有的互斥锁时：
1.  系统暂时将 L 的优先级提升到 H 的优先级 (`Priority(L) = Priority(H)`)。
2.  L 现在以高优先级运行，防止被中优先级任务 M 抢占。
3.  L 运行并释放锁 M。
4.  释放锁时，系统将 L 的优先级恢复为原始优先级 (`OriginalPrio`)。
5.  H 获得锁并运行。

**代码实现关键点 (`OS_MutexPend`):**
```c
if (CurrentTCB->Priority < p_mutex->Owner->Priority)
{
    if (p_mutex->Owner->State == TASK_READY)
    {
        // 按旧优先级摘除节点，再修改键并放入新的就绪队列。
        OS_ReadyListRemove(p_mutex->Owner);
        p_mutex->Owner->Priority = CurrentTCB->Priority;
        OS_ReadyListAdd(p_mutex->Owner);
    }
    else
    {
        // 阻塞持有者不在就绪表中；资源或时间唤醒时才按新优先级入队。
        p_mutex->Owner->Priority = CurrentTCB->Priority;
    }
}
```

---

## 4. 静态内存池设计 (Static Memory Pool)

为了避免内存碎片和分配时间的不确定性（相比于 `malloc`/`free`），SandOS 采用了**固定大小块的静态内存池**设计。

### 设计亮点
*   **无碎片化**: 内存池被划分为大小相同的块 (`BlockSize`)，不存在外部碎片。
*   **O(1) 分配与释放**:
    *   **空闲链表 (`FreeList`)**: 所有的空闲内存块通过单向链表串联起来。
    *   **指针存储**: 空闲块的头4个字节直接存储“下一个空闲块的地址”。这样不需要额外的 RAM 来维护链表结构。
*   **确定性**: 分配和释放的时间是固定的，非常适合实时系统。

### 内存结构图

```ascii
   OS_Mem Object             Memory Pool Area (RAM)
 +----------------+        +--------------------------+
 | Addr           |------->| Block 0 (Free)           |
 | ...            |        | [Next: Block 1 Addr]     |--+
 | FreeList       |--+     | Data...                  |  |
 +----------------+  |     +--------------------------+  |
                     |     | Block 1 (Free)           |<-+
                     +---->| [Next: Block 2 Addr]     |--+
                           | Data...                  |  |
                           +--------------------------+  |
                           | Block 2 (Used)           |<-+
                           | User Data...             |
                           |                          |
                           +--------------------------+
                           | Block 3 (Free)           |
                           | [Next: NULL]             |
                           | Data...                  |
                           +--------------------------+
```

### 工作原理
1.  **初始化 (`OS_MemInit`)**: 将连续内存切块，每个块的头部写入下一个块的地址，形成链表。
2.  **申请 (`OS_MemGet`)**: 取出 `FreeList` 指向的块，并将 `FreeList` 更新为该块指向的下一个地址。
3.  **释放 (`OS_MemPut`)**: 将释放的块插入到 `FreeList` 的头部（头插法）。

---
**SandOS** 旨在提供一个精简、可读且功能完备的实时内核教学与应用示例。
