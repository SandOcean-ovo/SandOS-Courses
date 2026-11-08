# ============================================================================
# 主机测试构建（GNU Make）
#
# 用法（在仓库根目录，Windows 下用 mingw32-make）:
#   mingw32-make -C tests/host test        # 构建并运行主机测试
#   mingw32-make -C tests/host neg         # 运行负例，确认测试框架会报失败
#   mingw32-make -C tests/host clean
#
# 设计说明:
#   - 直接编译 rtos/Src/os_core.c，不改内核源码；
#   - 强制包含 host_hooks.h，只替换测试构建中的 OS_ASSERT 宏；
#     保留内核原断言函数，避免宏改名造成重复定义；
#   - 用本目录的 os_board.h 代替 ports/ 下的板级头文件，所以不需要 CMSIS；
#   - 只验证可隔离的纯 C 逻辑，不能替代 QEMU/GDB 对异常、栈帧、上下文切换的验证。
# ============================================================================
ifeq ($(origin CC),default)
CC      := gcc
endif
ROOT    := ../..
BUILD   := build
CFLAGS  := -std=gnu11 -O0 -g -Wall -Wextra -Wno-unused-parameter \
           -DOS_BENCHMARK_ENABLE=0 -include host_hooks.h \
           -I. -I$(ROOT)/rtos/Inc -I$(ROOT)/rtos/Portable -I$(ROOT)/rtos/Portable/ARM_CM3

# 内核源文件 + 桩 + 测试
KERNEL  := $(ROOT)/rtos/Src/os_core.c host_stubs.c

all: test
all-build: $(BUILD)/test_host.exe

HEADERS := host_hooks.h os_board.h $(wildcard $(ROOT)/rtos/Inc/*.h) \
           $(ROOT)/rtos/Portable/os_common.h $(ROOT)/rtos/Portable/ARM_CM3/os_cpu.h

$(BUILD)/test_host.exe: $(KERNEL) test_main.c $(HEADERS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) $(KERNEL) test_main.c -o $@

$(BUILD):
	mkdir -p $@

test: $(BUILD)/test_host.exe
	./$(BUILD)/test_host.exe

# 同一测试程序的故意错误期望和未布防断言，必须恰好以 1 退出；崩溃不算通过。
neg: $(BUILD)/test_host.exe
	@for mode in --negative-check --negative-assert; do \
	    ./$(BUILD)/test_host.exe $$mode; rc=$$?; \
	    if [ $$rc -eq 1 ]; then echo "PASS  HOSTNEG $$mode (exit 1 as expected)"; \
	    else echo "FAIL  HOSTNEG $$mode (exit $$rc, expected 1)"; exit 1; fi; \
	done

clean:
	rm -rf $(BUILD)

.PHONY: all all-build test neg clean
