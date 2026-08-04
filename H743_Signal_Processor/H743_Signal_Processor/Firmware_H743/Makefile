##########################################################################################################################
# Makefile for STM32H743ZITx Signal Processor (matches CMakeLists.txt)
# 编译链: arm-none-eabi-gcc + OpenOCD/stlink-tools
##########################################################################################################################

# 工具链
PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc -x assembler-with-cpp
CP      = $(PREFIX)objcopy
SZ      = $(PREFIX)size

# 工程名 + 输出目录
TARGET  = H743_Signal_Processor
BUILD_DIR = build

# 默认目标
.DEFAULT_GOAL := all

# CPU / FPU
CPU     = -mcpu=cortex-m7
FPU     = -mfpu=fpv5-d16
FLOAT-ABI = -mfloat-abi=hard
MCU     = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

# 编译选项
CFLAGS  = $(MCU) -std=gnu11
CFLAGS += -DSTM32H743xx -DUSE_HAL_DRIVER -DUSE_PWR_LDO_SUPPLY -DUSE_FULL_LL_DRIVER
CFLAGS += -O2 -g3 -Wall -ffunction-sections -fdata-sections
CFLAGS += -Wno-unused-parameter -Wno-unused-variable
CFLAGS += -Wno-implicit-function-declaration

# 头文件路径
CFLAGS  += -I Core/Inc
CFLAGS  += -I ../Algorithm/Inc
CFLAGS  += -I ../Algorithm/Private
CFLAGS  += -I ../Protocol/Inc
CFLAGS  += -I Drivers/STM32H7xx_HAL_Driver/Inc
CFLAGS  += -I Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
CFLAGS  += -I Drivers/CMSIS/Device/ST/STM32H7xx/Include
CFLAGS  += -I Drivers/CMSIS/Include

# 链接
LDSCRIPT = STM32H743ZITX_FLASH.ld
LIBS     = -lc -lm -lnosys
LDFLAGS  = $(MCU) -specs=nano.specs -u _printf_float -u _scanf_float -T$(LDSCRIPT) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

# C 源文件
C_SOURCES = \
Core/Src/main.c \
Core/Src/processor_app.c \
Core/Src/spi_links.c \
Core/Src/syscalls.c \
Core/Src/stm32h7xx_hal_msp.c \
Core/Src/stm32h7xx_it.c \
Core/Src/system_stm32h7xx.c \
../Algorithm/Src/signal_analyzer.c \
../Algorithm/Src/clipped_harmonic_recovery.c \
../Algorithm/Src/signal_preprocess.c \
../Algorithm/Src/signal_fft_portable.c \
../Algorithm/Src/spectrum_detect.c \
../Algorithm/Src/czt_zoom.c \
../Algorithm/Src/harmonic_model.c \
../Algorithm/Src/orthogonal_fit.c \
../Algorithm/Src/signal_metrics.c \
../Protocol/Src/h743_crc32.c \
../Protocol/Src/h743_result_protocol.c \
../Protocol/Src/h743_set_fs_protocol.c \
../Protocol/Src/h743_measurement_pages.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi_ex.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_usart.c

# 汇编源文件
ASM_SOURCES = \
Startup/startup_stm32h743xx.s

# 目标
OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	@echo "  CC    $<"
	@$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	@echo "  AS    $<"
	@$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	@echo "  LD    $@"
	@$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@echo "  SZ    $@"
	@$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "  CP    $@"
	@$(CP) -O ihex $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "  CP    $@"
	@$(CP) -O binary -S $< $@

$(BUILD_DIR):
	@mkdir -p $@

.PHONY: all clean flash stflash debug
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

clean:
	@rm -rf $(BUILD_DIR)

# 烧录: ST-Link + OpenOCD (SWD),可通过 STLINK_SERIAL_H7 选择探针
STLINK_SERIAL_H7 ?=
STLINK_SELECT_H7 := $(if $(strip $(STLINK_SERIAL_H7)),-c "adapter serial $(STLINK_SERIAL_H7)",)
flash: all
	openocd -f interface/stlink.cfg $(STLINK_SELECT_H7) \
		-c "transport select hla_swd" \
		-f target/stm32h7x.cfg \
		-c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"

# 调试服务器
debug: all
	openocd -f interface/stlink.cfg $(STLINK_SELECT_H7) \
		-c "transport select hla_swd" \
		-f target/stm32h7x.cfg

# 备选烧录: stlink-tools (对克隆 ST-Link 更友好)
stflash: all
	st-flash --connect-under-reset write $(BUILD_DIR)/$(TARGET).bin 0x08000000
