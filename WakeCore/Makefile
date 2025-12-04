######################################
# WakeCore (STM32U0) Makefile
######################################
# Inherits common configuration from root Makefile (via export)

######################################
# Helper Functions
######################################
rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

######################################
# Target Configuration
######################################
TARGET = $(PROJECT_NAME)_WakeCore

# Local build directory
BUILD_DIR = build

######################################
# Tools and Flash Address (local defaults)
######################################
ifndef FLASHER
FLASHER = STM32_Programmer_CLI
endif
FLASH_ADDR = 0x8000000

######################################
# Toolchain Overrides (STM32U0 - Cortex-M0+)
######################################
# Override MCU flags from root (which are set for Cortex-M55)
CPU = -mcpu=cortex-m0plus
MCU_FLAGS = $(CPU) -mthumb

######################################
# Source Files
######################################
# C source files - Core
C_SOURCES =
C_SOURCES += Core/Src/main.c
C_SOURCES += Core/Src/syscalls.c
C_SOURCES += Core/Src/sysmem.c
C_SOURCES += Core/Src/app_freertos.c
C_SOURCES += Core/Src/stm32u0xx_it.c
C_SOURCES += Core/Src/stm32u0xx_hal_msp.c
C_SOURCES += Core/Src/system_stm32u0xx.c
C_SOURCES += $(wildcard Core/Src/crc.c Core/Src/dma.c Core/Src/gpio.c)
C_SOURCES += $(wildcard Core/Src/iwdg.c Core/Src/rtc.c Core/Src/spi.c)
C_SOURCES += $(wildcard Core/Src/usart.c)

# Custom - User & Components & Libs
C_SOURCES += Custom/User/app.c
C_SOURCES += Custom/Components/n6_comm/n6_comm.c
C_SOURCES += Custom/Components/pwr_manager/pwr_manager.c
C_SOURCES += Custom/Components/ms_bridging/ms_bridging.c
C_SOURCES += Custom/Lib/cJSON/cJSON.c
C_SOURCES += Custom/Lib/wic_log/wic_log.c
C_SOURCES += Custom/Components/pir/pir.c

# Middlewares - FreeRTOS (CMSIS-RTOS2 wrapper + core + port + heap)
C_SOURCES += Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.c
C_SOURCES += Middlewares/Third_Party/FreeRTOS/Source/croutine.c
C_SOURCES += Middlewares/Third_Party/FreeRTOS/Source/event_groups.c
C_SOURCES += Middlewares/Third_Party/FreeRTOS/Source/list.c
C_SOURCES += Middlewares/Third_Party/FreeRTOS/Source/queue.c
C_SOURCES += Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c
C_SOURCES += Middlewares/Third_Party/FreeRTOS/Source/tasks.c
C_SOURCES += Middlewares/Third_Party/FreeRTOS/Source/timers.c
C_SOURCES += Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM0/port.c
C_SOURCES += Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c

# HAL drivers (STM32U0xx)
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_cortex.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_crc.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_crc_ex.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_dma.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_dma_ex.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_exti.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_flash.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_flash_ex.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_gpio.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_iwdg.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_pwr.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_pwr_ex.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_rcc.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_rcc_ex.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_rtc.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_rtc_ex.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_spi.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_spi_ex.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_uart.c
C_SOURCES += Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_uart_ex.c

# Assembly source files (.s lowercase)
ASM_SOURCES =
ASM_SOURCES += Core/Startup/startup_stm32u073kbux.s

######################################
# C Compiler Defines
######################################
# C_DEFS = $(COMMON_DEFS)
C_DEFS += -DDEBUG
C_DEFS += -DUSE_HAL_DRIVER
C_DEFS += -DSTM32U073xx
C_DEFS += -DSTM32_THREAD_SAFE_STRATEGY=2
C_DEFS += -DWAKECORE_VERSION=\"$(WAKECORE_VERSION)\"

######################################
# Assembly-specific Defines
######################################
AS_DEFS =
AS_DEFS += -DDEBUG

######################################
# Include Paths
######################################
C_INCLUDES =

# Core
C_INCLUDES += -ICore/Inc
C_INCLUDES += -ICore/ThreadSafe

# Custom
C_INCLUDES += -ICustom/User
C_INCLUDES += -ICustom/Lib/cJSON
C_INCLUDES += -ICustom/Lib/wic_log
C_INCLUDES += -ICustom/Components/n6_comm
C_INCLUDES += -ICustom/Components/pwr_manager
C_INCLUDES += -ICustom/Components/ms_bridging
C_INCLUDES += -ICustom/Components/pir

# Drivers - HAL & CMSIS
C_INCLUDES += -IDrivers/STM32U0xx_HAL_Driver/Inc
C_INCLUDES += -IDrivers/STM32U0xx_HAL_Driver/Inc/Legacy
C_INCLUDES += -IDrivers/CMSIS/Device/ST/STM32U0xx/Include
C_INCLUDES += -IDrivers/CMSIS/Include

# Middlewares - CMSIS RTOS2 & FreeRTOS
C_INCLUDES += -IMiddlewares/Third_Party/CMSIS/RTOS2/Include
C_INCLUDES += -IMiddlewares/Third_Party/FreeRTOS/Source/include
C_INCLUDES += -IMiddlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
C_INCLUDES += -IMiddlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM0

######################################
# Compiler and Linker Flags
######################################
# C compiler flags (inherits COMMON_CFLAGS)
CFLAGS = $(filter-out -mcpu=% -mfpu=% -mfloat-abi=%,$(COMMON_CFLAGS)) $(C_DEFS) $(C_INCLUDES) $(MCU_FLAGS)
# Generate dependency information
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

# Assembler flags (inherits COMMON_ASFLAGS)
ASFLAGS = $(filter-out -mcpu=% -mfpu=% -mfloat-abi=%,$(COMMON_ASFLAGS)) $(AS_DEFS) $(MCU_FLAGS)

######################################
# Linker Configuration
######################################
# Linker script
LDSCRIPT = STM32U073KBUX_FLASH.ld

# Libraries
LIBS = -lc -lm -lnosys

LIBDIR =

# Linker flags (inherits COMMON_LDFLAGS from root)
LDFLAGS = $(filter-out -mcpu=% -mfpu=% -mfloat-abi=%,$(COMMON_LDFLAGS)) $(MCU_FLAGS) -T$(LDSCRIPT)
LDFLAGS += $(LIBDIR)
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -static

######################################
# Build Targets
######################################
.PHONY: all
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

######################################
# Build Rules
######################################
# Object files
OBJECTS = $(addprefix $(BUILD_DIR)/, $(C_SOURCES:.c=.o))
OBJECTS += $(addprefix $(BUILD_DIR)/, $(ASM_SOURCES:.s=.o))

# Pre-create all build directories (avoid race conditions in parallel builds)
.PHONY: build-dirs
build-dirs: $(BUILD_DIR)
	@mkdir -p $(sort $(dir $(OBJECTS)))

# Make all objects depend on directories
$(OBJECTS): | build-dirs

# Compile C source files
$(BUILD_DIR)/%.o: %.c Makefile
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

# Compile assembly source files (.s)
$(BUILD_DIR)/%.o: %.s Makefile
	@echo "AS $<"
	@$(AS) -c $(ASFLAGS) $< -o $@

# Link ELF file
$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	@echo "LD $@"
	@$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@$(SZ) $@

# Generate HEX file
$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "HEX $@"
	@$(HEX) $< $@

# Generate BIN file
$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "BIN $@"
	@$(BIN) $< $@

# Create build directory
$(BUILD_DIR):
	@mkdir -p $@

######################################
# Clean
######################################
.PHONY: clean
clean:
	@echo "Cleaning WakeCore build files..."
	@rm -rf $(BUILD_DIR)

######################################
# Dependencies
######################################
-include $(call rwildcard,$(BUILD_DIR),*.d)

######################################
# Debug Information
######################################
.PHONY: info
info:
	@echo "WakeCore Makefile Configuration:"
	@echo "  TARGET      = $(TARGET)"
	@echo "  BUILD_DIR   = $(BUILD_DIR)"
	@echo "  C_SOURCES   = $(words $(C_SOURCES)) files"
	@echo "  ASM_SOURCES = $(words $(ASM_SOURCES)) files"
	@echo "  LDSCRIPT    = $(LDSCRIPT)"

######################################
# Flash (STM32U0 internal, no external loader)
######################################
.PHONY: flash
flash: $(BUILD_DIR)/$(TARGET).bin
	@echo "Flashing WakeCore (U0) to $(FLASH_ADDR)..."
	@$(FLASHER) -c port=SWD mode=UR -w $(BUILD_DIR)/$(TARGET).bin $(FLASH_ADDR) -v -rst
	@echo "WakeCore flash complete"

