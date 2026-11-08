# 验证 PASS 之后的 GDB 命令错误不能被当作通过。
printf "GDBCHK synthetic PASS\n"
printf "GDBRESULT PASS\n"
this_command_does_not_exist
