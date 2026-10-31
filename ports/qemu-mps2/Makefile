# QEMU mps2-an385 构建。用法：mingw32-make -C ports/qemu-mps2
CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
ROOT    := ../..
# APP 指定应用/测试源文件（默认 main.c），产物为 build/<名字>.elf
APP     ?= main.c
NAME    := $(basename $(notdir $(APP)))
BUILD   := build
TARGET  := $(BUILD)/$(NAME).elf

SRCS := $(filter-out main.c,$(wildcard *.c)) $(APP)         $(ROOT)/rtos/Src/os_core.c         $(ROOT)/rtos/Portable/ARM_CM3/os_cpu.c         $(ROOT)/rtos/Portable/ARM_CM3/os_cpu_a_gcc.S
# benchmark.c 依赖 DWT，默认不参与构建（OS_BENCHMARK_ENABLE=0）

INCS := -I. -I$(ROOT)/rtos/Inc -I$(ROOT)/rtos/Portable -I$(ROOT)/rtos/Portable/ARM_CM3 -I$(ROOT)/third_party/cmsis
CFLAGS  := -mcpu=cortex-m3 -mthumb -Os -g3 -Wall -Wextra -ffunction-sections -fdata-sections \
           -DOS_BENCHMARK_ENABLE=0 $(INCS)
LDFLAGS := -mcpu=cortex-m3 -mthumb -T link.ld -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=$(BUILD)/$(NAME).map \
           --specs=nano.specs --specs=nosys.specs

OBJS := $(patsubst %,$(BUILD)/%.o,$(notdir $(basename $(SRCS))))
vpath %.c . $(dir $(APP)) $(ROOT)/rtos/Src $(ROOT)/rtos/Portable/ARM_CM3
vpath %.S $(ROOT)/rtos/Portable/ARM_CM3

all: $(TARGET)
$(TARGET): $(OBJS) link.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CROSS)size $@
$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD):
	mkdir -p $@
clean:
	rm -rf $(BUILD)
.PHONY: all clean
