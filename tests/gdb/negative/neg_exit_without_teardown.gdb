# GDBNEG-05：先输出看似通过的结果，随后让目标自然退出，不执行清理握手。
# 即使 GDB/QEMU 都正常退出 0，缺少主动 kill 的确认也必须判失败。
set pagination off
set confirm off
printf "GDBCHK premature_exit PASS\n"
printf "GDBRESULT PASS\n"
continue
quit
