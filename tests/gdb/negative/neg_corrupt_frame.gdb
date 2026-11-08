# ============================================================================
# neg_corrupt_frame.gdb —— 负例 GDBNEG-01：故意破坏栈帧，检查应当报失败
#
# 在 PendSV_Exit 断点处，先把栈帧里的 PC 和 xPSR 改成错误值，再复用同一套检查。
# 预期：run_gdb.py 判失败（退出码 1），至少 frame_pc_is_task_entry 与 xpsr_thumb_bit 报 FAIL。
# 目的：证明 pendsv_frame.gdb 的检查不是“永远通过”。
# ============================================================================
set pagination off
set confirm off
break PendSV_Exit
continue

# 破坏栈帧：PC 改成 0x1234，xPSR 清掉 Thumb 位
set $frame = (unsigned int *)$psp
set $frame[6] = 0x1234
set $frame[7] = 0x0

set $fails = 0
source tests/gdb/initial_frame_checks.gdb
if $fails == 0
  printf "GDBRESULT PASS\n"
else
  printf "GDBRESULT FAIL fails=%d\n", $fails
end
kill
quit
