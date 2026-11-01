# ============================================================================
# QEMU mps2-an385 构建文件（GNU Make）
#
# 用法（在仓库根目录，Windows 下用 mingw32-make，其他平台用 make）：
#   mingw32-make -C ports/qemu-mps2                       # 构建默认冒烟程序 main.c
#   mingw32-make -C ports/qemu-mps2 APP=<源文件路径>       # 构建指定的应用/测试程序
#   mingw32-make -C ports/qemu-mps2 clean                 # 删除 build/
#
# 产物：build/<APP 文件名去掉扩展名>.elf 与同名 .map
#
# 设计说明：
#   - 本文件只负责 GCC + QEMU 路线，不维护第二套构建系统；
#   - 内核源码直接从 ../../rtos 引用，不复制；
#   - benchmark.c 依赖 DWT 周期计数器，QEMU 不适合测周期，所以不参与构建，
#     并通过 -DOS_BENCHMARK_ENABLE=0 关闭内核里的 benchmark 代码。
# ============================================================================

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
ROOT    := ../..

# APP：应用或测试程序的 .c 文件（默认 main.c）。
# 板级公共文件（startup.c、uart.c、syscalls.c）总是参与构建，所以要把 main.c 从通配结果里排除，
# 再追加 APP，避免出现两个 main。
APP     ?= main.c
NAME    := $(basename $(notdir $(APP)))
BUILD   := build
TARGET  := $(BUILD)/$(NAME).elf

SRCS := $(filter-out main.c,$(wildcard *.c)) $(APP) \
        $(ROOT)/rtos/Src/os_core.c \
        $(ROOT)/rtos/Portable/ARM_CM3/os_cpu.c \
        $(ROOT)/rtos/Portable/ARM_CM3/os_cpu_a_gcc.S

# 头文件搜索路径：本目录的 os_board.h、内核头文件、移植层头文件、CMSIS Core
INCS := -I. \
        -I$(ROOT)/rtos/Inc \
        -I$(ROOT)/rtos/Portable \
        -I$(ROOT)/rtos/Portable/ARM_CM3 \
        -I$(ROOT)/third_party/cmsis

# 编译选项：Cortex-M3 Thumb 指令；-Os 优化大小；-g3 保留完整调试信息（GDB 用）；
# -ffunction-sections/-fdata-sections 配合链接时 --gc-sections 去掉未用代码
CFLAGS  := -mcpu=cortex-m3 -mthumb -Os -g3 -Wall -Wextra \
           -ffunction-sections -fdata-sections \
           -DOS_BENCHMARK_ENABLE=0 $(INCS)

# 链接选项：使用本目录链接脚本；nano.specs 使用体积更小的 newlib-nano；
# nosys.specs 提供空的系统调用（真正的 _write 等在 syscalls.c）；
# --no-warn-rwx-segments 屏蔽“LOAD 段同时可读写执行”的提示（Cortex-M 裸机的正常布局）
LDFLAGS := -mcpu=cortex-m3 -mthumb -T link.ld \
           -Wl,--gc-sections -Wl,--no-warn-rwx-segments \
           -Wl,-Map=$(BUILD)/$(NAME).map \
           --specs=nano.specs --specs=nosys.specs

# 目标文件统一放在 build/，用“文件名.扩展名.o”之外的简化命名（去掉目录与扩展名）
OBJS := $(patsubst %,$(BUILD)/%.o,$(notdir $(basename $(SRCS))))

# 源文件所在目录，供 make 在这些位置查找 %.c / %.S
vpath %.c . $(dir $(APP)) $(ROOT)/rtos/Src $(ROOT)/rtos/Portable/ARM_CM3
vpath %.S $(ROOT)/rtos/Portable/ARM_CM3

all: $(TARGET)

# 链接：链接脚本变化也要重新链接
$(TARGET): $(OBJS) link.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CROSS)size $@

# 编译 C 文件；“| $(BUILD)” 表示 build 目录只需存在，不因其时间戳触发重编
$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# 编译汇编文件（.S 会先经过预处理器）
$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
