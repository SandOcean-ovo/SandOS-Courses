# 共用首次栈帧检查；正例与破坏栈帧负例调用同一份判定。
# ---- 取出栈帧 ----
set $frame = (unsigned int *)$psp
set $f_r0   = $frame[0]
set $f_lr   = $frame[5]
set $f_pc   = $frame[6]
set $f_xpsr = $frame[7]

printf "GDBINFO psp=0x%08x r0=0x%08x lr=0x%08x pc=0x%08x xpsr=0x%08x\n", $psp, $f_r0, $f_lr, $f_pc, $f_xpsr
printf "GDBINFO CurrentTCB=0x%08x tcb_a=0x%08x tcb_b=0x%08x\n", CurrentTCB, &tcb_a, &tcb_b

# ---- 检查 1：本用例预构造的任务栈帧按 8 字节对齐----
if ((unsigned int)$psp & 7) == 0
  printf "GDBCHK psp_aligned PASS\n"
else
  printf "GDBCHK psp_aligned FAIL psp=0x%08x\n", $psp
  set $fails = $fails + 1
end

# ---- 检查 2：栈帧中的 PC 是某个任务的入口（首次切换运行的任务之一）----
if $f_pc == ((unsigned int)task_a | 1) || $f_pc == ((unsigned int)task_b | 1)
  printf "GDBCHK frame_pc_is_task_entry PASS\n"
else
  printf "GDBCHK frame_pc_is_task_entry FAIL pc=0x%08x\n", $f_pc
  set $fails = $fails + 1
end

# ---- 检查 3：xPSR 的 Thumb 位（bit24）已置位，否则返回时会触发 UsageFault ----
if ($f_xpsr & 0x01000000) != 0
  printf "GDBCHK xpsr_thumb_bit PASS\n"
else
  printf "GDBCHK xpsr_thumb_bit FAIL xpsr=0x%08x\n", $f_xpsr
  set $fails = $fails + 1
end

# ---- 检查 4：栈帧里的 R0 为任务参数（本用例创建任务时传 NULL）----
if $f_r0 == 0
  printf "GDBCHK frame_r0_is_task_param PASS\n"
else
  printf "GDBCHK frame_r0_is_task_param FAIL r0=0x%08x\n", $f_r0
  set $fails = $fails + 1
end

# ---- 检查 5：栈帧里的 LR 指向 OS_TaskReturn（任务函数返回时的落点）----
if $f_lr == ((unsigned int)OS_TaskReturn | 1)
  printf "GDBCHK frame_lr_is_task_return PASS\n"
else
  printf "GDBCHK frame_lr_is_task_return FAIL lr=0x%08x\n", $f_lr
  set $fails = $fails + 1
end

# ---- 检查 6：CurrentTCB 已指向被恢复的任务，且 PSP 位于该任务自己的栈数组内 ----
if CurrentTCB == &tcb_a
  set $lo = (unsigned int)&stack_a[0]
  set $hi = (unsigned int)&stack_a[256]
  set $expect_entry = ((unsigned int)task_a | 1)
else
  if CurrentTCB != &tcb_b
    printf "GDBCHK known_current_tcb FAIL\n"
    set $fails = $fails + 1
  end
  set $lo = (unsigned int)&stack_b[0]
  set $hi = (unsigned int)&stack_b[256]
  set $expect_entry = ((unsigned int)task_b | 1)
end
if (unsigned int)$psp >= $lo && (unsigned int)$psp + 32 <= $hi
  printf "GDBCHK psp_inside_task_stack PASS\n"
else
  printf "GDBCHK psp_inside_task_stack FAIL psp=0x%08x range=[0x%08x,0x%08x)\n", $psp, $lo, $hi
  set $fails = $fails + 1
end

# ---- 检查 7：CurrentTCB 对应的任务入口与栈帧 PC 一致（防止恢复了别的任务的栈）----
if $f_pc == $expect_entry
  printf "GDBCHK tcb_matches_frame PASS\n"
else
  printf "GDBCHK tcb_matches_frame FAIL pc=0x%08x expected=0x%08x\n", $f_pc, $expect_entry
  set $fails = $fails + 1
end
