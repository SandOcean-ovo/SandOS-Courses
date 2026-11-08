# ============================================================================
# pendsv_frame.gdb —— 验证首次 PendSV 切换后新任务的栈帧（用例 GDB-01）
#
# 用法：由 tests/scripts/run_gdb.py 启动 QEMU、连接 GDB 后加载本脚本。
#   python tests/scripts/run_gdb.py <elf> tests/gdb/pendsv_frame.gdb
#
# 检查时机：在 PendSV_Exit 下断点。此处 R4-R11 已恢复、PSP 已指向新任务的
# 硬件栈帧起点、尚未执行 bx lr，所以 PSP 指向的 8 个字就是“即将被硬件弹出”的内容。
# 不要在别处断下后直接用 $psp 解释：不同断点下的 PSP 含义不同（参见排错记录）。
#
# 关于 Thumb 位：Cortex-M 的函数指针最低位恒为 1（表示 Thumb 状态）。
# OS_StackInit 把函数指针原样写进栈帧，所以栈帧里的 PC/LR 是 0x...1；
# 而 GDB 读符号地址时得到的是去掉最低位的值（nm 也是）。比较时统一给符号地址 "| 1"。
#
# 栈帧布局（从 PSP 向高地址）：
#   [0]=R0  [1]=R1  [2]=R2  [3]=R3  [4]=R12  [5]=LR  [6]=PC  [7]=xPSR
#
# 输出每项检查的 "GDBCHK <名称> <PASS|FAIL> ..."，最后输出 "GDBRESULT PASS|FAIL"。
# ============================================================================
set pagination off
set confirm off
set $fails = 0

tbreak PendSV_Exit
continue

source tests/gdb/initial_frame_checks.gdb

source tests/gdb/context_switch.gdb

if $fails == 0
  printf "GDBRESULT PASS\n"
else
  printf "GDBRESULT FAIL fails=%d\n", $fails
end
# kill 在 Linux 上可让 QEMU 正常退出；标记只在该命令成功后输出。
printf "GDBTEARDOWN BEGIN\n"
kill
printf "GDBTEARDOWN DONE\n"
quit
