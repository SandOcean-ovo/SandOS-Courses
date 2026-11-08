# GDB-01 后半段：检查一次真实保存、另一个任务的恢复及旧任务再次恢复。
# 所有断点都在指令执行前停下；不把 Handler 当前 PC/LR 当作栈帧 PC/LR。
# 先完成首次异常返回，暂停 SysTick 避免单步时被 tick 打断。
set $tick_ctrl = *(unsigned int *)0xe000e010
# 单步前停止计时并清除已挂起的 SysTick；仅停 CTRL 不会清除 pending。
set *(unsigned int *)0xe000e010 = 0
set *(unsigned int *)0xe000ed04 = 0x02000000
set $entry_pc = $frame[6] & ~1
stepi
if (unsigned int)$pc == $entry_pc && $r0 == $f_r0
  printf "GDBCHK first_exception_return PASS\n"
else
  printf "GDBCHK first_exception_return FAIL\n"
  set $fails = $fails + 1
end
set *(unsigned int *)0xe000e010 = $tick_ctrl

tbreak PendSV_Handler if CurrentTCB != 0 && CurrentTCB != NextTCB
continue
set $old = CurrentTCB
set $old_psp = (unsigned int)$psp
# 测试注入：硬件帧已形成、软件保存尚未发生。用互不相同的值检查寄存器顺序，
# 避免 R4-R11 恰巧全为零时漏掉错位；在旧任务再次返回的第一条指令前结束测试。
set $r4 = 0x44444444
set $r5 = 0x55555555
set $r6 = 0x66666666
set $r7 = 0x77777777
set $r8 = 0x88888888
set $r9 = 0x99999999
set $r10 = 0xaaaaaaaa
set $r11 = 0xbbbbbbbb
set $saved_r4 = $r4
set $saved_r5 = $r5
set $saved_r6 = $r6
set $saved_r7 = $r7
set $saved_r8 = $r8
set $saved_r9 = $r9
set $saved_r10 = $r10
set $saved_r11 = $r11
set $hw_0 = ((unsigned int *)$old_psp)[0]
set $hw_1 = ((unsigned int *)$old_psp)[1]
set $hw_2 = ((unsigned int *)$old_psp)[2]
set $hw_3 = ((unsigned int *)$old_psp)[3]
set $hw_4 = ((unsigned int *)$old_psp)[4]
set $hw_5 = ((unsigned int *)$old_psp)[5]
set $hw_6 = ((unsigned int *)$old_psp)[6]
set $hw_7 = ((unsigned int *)$old_psp)[7]
tbreak PendSV_Saved
continue
set $sw = (unsigned int *)$old->stackPtr
if (unsigned int)$sw == $old_psp - 32
  printf "GDBCHK saved_stack_pointer PASS\n"
else
  printf "GDBCHK saved_stack_pointer FAIL\n"
  set $fails = $fails + 1
end
if $sw[0] == $saved_r4
  printf "GDBCHK saved_r4 PASS\n"
else
  printf "GDBCHK saved_r4 FAIL\n"
  set $fails = $fails + 1
end
if $sw[1] == $saved_r5
  printf "GDBCHK saved_r5 PASS\n"
else
  printf "GDBCHK saved_r5 FAIL\n"
  set $fails = $fails + 1
end
if $sw[2] == $saved_r6
  printf "GDBCHK saved_r6 PASS\n"
else
  printf "GDBCHK saved_r6 FAIL\n"
  set $fails = $fails + 1
end
if $sw[3] == $saved_r7
  printf "GDBCHK saved_r7 PASS\n"
else
  printf "GDBCHK saved_r7 FAIL\n"
  set $fails = $fails + 1
end
if $sw[4] == $saved_r8
  printf "GDBCHK saved_r8 PASS\n"
else
  printf "GDBCHK saved_r8 FAIL\n"
  set $fails = $fails + 1
end
if $sw[5] == $saved_r9
  printf "GDBCHK saved_r9 PASS\n"
else
  printf "GDBCHK saved_r9 FAIL\n"
  set $fails = $fails + 1
end
if $sw[6] == $saved_r10
  printf "GDBCHK saved_r10 PASS\n"
else
  printf "GDBCHK saved_r10 FAIL\n"
  set $fails = $fails + 1
end
if $sw[7] == $saved_r11
  printf "GDBCHK saved_r11 PASS\n"
else
  printf "GDBCHK saved_r11 FAIL\n"
  set $fails = $fails + 1
end
set $i = 0
set $hw_ok = 1
while $i < 8
  if $i == 0 && $sw[8 + $i] != $hw_0
    set $hw_ok = 0
  end
  if $i == 1 && $sw[8 + $i] != $hw_1
    set $hw_ok = 0
  end
  if $i == 2 && $sw[8 + $i] != $hw_2
    set $hw_ok = 0
  end
  if $i == 3 && $sw[8 + $i] != $hw_3
    set $hw_ok = 0
  end
  if $i == 4 && $sw[8 + $i] != $hw_4
    set $hw_ok = 0
  end
  if $i == 5 && $sw[8 + $i] != $hw_5
    set $hw_ok = 0
  end
  if $i == 6 && $sw[8 + $i] != $hw_6
    set $hw_ok = 0
  end
  if $i == 7 && $sw[8 + $i] != $hw_7
    set $hw_ok = 0
  end
  set $i = $i + 1
end
if $hw_ok
  printf "GDBCHK hardware_frame_preserved PASS\n"
else
  printf "GDBCHK hardware_frame_preserved FAIL\n"
  set $fails = $fails + 1
end

tbreak PendSV_Exit
continue
set $new_sw = (unsigned int *)CurrentTCB->stackPtr
if CurrentTCB != $old && (unsigned int)$psp == (unsigned int)($new_sw + 8)
  printf "GDBCHK next_task_psp PASS\n"
else
  printf "GDBCHK next_task_psp FAIL\n"
  set $fails = $fails + 1
end
if $r4 == $new_sw[0]
  printf "GDBCHK restored_r4 PASS\n"
else
  printf "GDBCHK restored_r4 FAIL\n"
  set $fails = $fails + 1
end
if $r5 == $new_sw[1]
  printf "GDBCHK restored_r5 PASS\n"
else
  printf "GDBCHK restored_r5 FAIL\n"
  set $fails = $fails + 1
end
if $r6 == $new_sw[2]
  printf "GDBCHK restored_r6 PASS\n"
else
  printf "GDBCHK restored_r6 FAIL\n"
  set $fails = $fails + 1
end
if $r7 == $new_sw[3]
  printf "GDBCHK restored_r7 PASS\n"
else
  printf "GDBCHK restored_r7 FAIL\n"
  set $fails = $fails + 1
end
if $r8 == $new_sw[4]
  printf "GDBCHK restored_r8 PASS\n"
else
  printf "GDBCHK restored_r8 FAIL\n"
  set $fails = $fails + 1
end
if $r9 == $new_sw[5]
  printf "GDBCHK restored_r9 PASS\n"
else
  printf "GDBCHK restored_r9 FAIL\n"
  set $fails = $fails + 1
end
if $r10 == $new_sw[6]
  printf "GDBCHK restored_r10 PASS\n"
else
  printf "GDBCHK restored_r10 FAIL\n"
  set $fails = $fails + 1
end
if $r11 == $new_sw[7]
  printf "GDBCHK restored_r11 PASS\n"
else
  printf "GDBCHK restored_r11 FAIL\n"
  set $fails = $fails + 1
end
tbreak PendSV_Exit if CurrentTCB == $old
continue
if (unsigned int)$psp == $old_psp
  printf "GDBCHK old_task_psp_restored PASS\n"
else
  printf "GDBCHK old_task_psp_restored FAIL\n"
  set $fails = $fails + 1
end
if $r4 == $saved_r4
  printf "GDBCHK resumed_r4 PASS\n"
else
  printf "GDBCHK resumed_r4 FAIL\n"
  set $fails = $fails + 1
end
if $r5 == $saved_r5
  printf "GDBCHK resumed_r5 PASS\n"
else
  printf "GDBCHK resumed_r5 FAIL\n"
  set $fails = $fails + 1
end
if $r6 == $saved_r6
  printf "GDBCHK resumed_r6 PASS\n"
else
  printf "GDBCHK resumed_r6 FAIL\n"
  set $fails = $fails + 1
end
if $r7 == $saved_r7
  printf "GDBCHK resumed_r7 PASS\n"
else
  printf "GDBCHK resumed_r7 FAIL\n"
  set $fails = $fails + 1
end
if $r8 == $saved_r8
  printf "GDBCHK resumed_r8 PASS\n"
else
  printf "GDBCHK resumed_r8 FAIL\n"
  set $fails = $fails + 1
end
if $r9 == $saved_r9
  printf "GDBCHK resumed_r9 PASS\n"
else
  printf "GDBCHK resumed_r9 FAIL\n"
  set $fails = $fails + 1
end
if $r10 == $saved_r10
  printf "GDBCHK resumed_r10 PASS\n"
else
  printf "GDBCHK resumed_r10 FAIL\n"
  set $fails = $fails + 1
end
if $r11 == $saved_r11
  printf "GDBCHK resumed_r11 PASS\n"
else
  printf "GDBCHK resumed_r11 FAIL\n"
  set $fails = $fails + 1
end
# PREPARE_OLD_RETURN：仅隔离此单步观察窗口，不用于验证并发中断。
# 单步前停止计时并清除已挂起的 SysTick；仅停 CTRL 不会清除 pending。
set *(unsigned int *)0xe000e010 = 0
set *(unsigned int *)0xe000ed04 = 0x02000000
printf "GDBINFO before_return icsr=0x%08x xpsr=0x%08x expected_pc=0x%08x\n", *(unsigned int *)0xe000ed04, $xpsr, ($hw_6 & ~1)
stepi
printf "GDBINFO after_return pc=0x%08x lr=0x%08x xpsr=0x%08x\n", $pc, $lr, $xpsr
if (unsigned int)$pc == ($hw_6 & ~1) && $r0 == $hw_0 && $r1 == $hw_1 && $r2 == $hw_2 && $r3 == $hw_3 && $r12 == $hw_4 && $lr == $hw_5
  printf "GDBCHK old_task_exception_return PASS\n"
else
  printf "GDBCHK old_task_exception_return FAIL\n"
  set $fails = $fails + 1
end
