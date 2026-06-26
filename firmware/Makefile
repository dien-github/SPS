PROJECT := sps_pcd_mcu
BUILD_DIR := build

CROSS_COMPILE ?= arm-none-eabi-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE := $(CROSS_COMPILE)size

MCU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
COMMON_DEFS := -DSTM32F401xC -DUSE_FULL_LL_DRIVER
COMMON_INCLUDES := \
  -ICore/Inc \
  -Iapp \
  -Idrivers \
  -Imiddleware \
  -Iservices \
  -Ibootloader \
  -Idrivers/CMSIS/Include \
  -Idrivers/CMSIS/Device/ST/STM32F4xx/Include

FREERTOS_INCLUDES := \
  -IMiddlewares/Third_Party/FreeRTOS/Source/include \
  -IMiddlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F

CFLAGS := $(MCU_FLAGS) $(COMMON_DEFS) $(COMMON_INCLUDES) -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fno-common -Os -g3
ASFLAGS := $(MCU_FLAGS) -x assembler-with-cpp $(COMMON_DEFS) $(COMMON_INCLUDES) -g3
LDFLAGS_COMMON := $(MCU_FLAGS) --specs=nosys.specs --specs=nano.specs -Wl,--gc-sections -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments -Wl,--start-group -lc -lm -Wl,--end-group

STARTUP := Core/Startup/startup_stm32f401ccux.s
SYSTEM_SRC := Core/Src/system_stm32f4xx.c Core/Src/stm32f4xx_it.c Core/Src/syscalls.c

APP_LINKER ?= linker/app_slot_a.ld
APP_SRCS := \
  app/app_main.c \
  drivers/flash_ll.c \
  drivers/gpio_relay_ll.c \
  drivers/pwm_ir_ll.c \
  drivers/system_clock.c \
  drivers/timer_ll.c \
  drivers/uart_ll.c \
  drivers/watchdog_ll.c \
  middleware/command_queue.c \
  middleware/crc16_ccitt.c \
  middleware/crc32.c \
  middleware/frame_parser.c \
  middleware/ring_buffer.c \
  middleware/timebase.c \
  services/command_dispatcher.c \
  services/ir_control_service.c \
  services/ir_daikin_table.c \
  services/ota_update_manager.c \
  services/projector_service.c \
  services/relay_control_service.c \
  services/telemetry_status_manager.c \
  bootloader/boot_metadata.c \
  Middlewares/Third_Party/FreeRTOS/Source/croutine.c \
  Middlewares/Third_Party/FreeRTOS/Source/event_groups.c \
  Middlewares/Third_Party/FreeRTOS/Source/list.c \
  Middlewares/Third_Party/FreeRTOS/Source/queue.c \
  Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c \
  Middlewares/Third_Party/FreeRTOS/Source/tasks.c \
  Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c

BOOT_LINKER := bootloader/linker_bootloader.ld
BOOT_SRCS := \
  bootloader/boot_main.c \
  bootloader/boot_flash.c \
  bootloader/boot_metadata.c \
  bootloader/boot_crc.c \
  bootloader/boot_jump.c \
  drivers/system_clock.c \
  middleware/crc32.c

APP_OBJS := $(addprefix $(BUILD_DIR)/app/,$(APP_SRCS:.c=.o)) $(BUILD_DIR)/app/$(STARTUP:.s=.o) $(addprefix $(BUILD_DIR)/app/,$(SYSTEM_SRC:.c=.o))
BOOT_OBJS := $(addprefix $(BUILD_DIR)/boot/,$(BOOT_SRCS:.c=.o)) $(BUILD_DIR)/boot/$(STARTUP:.s=.o) $(BUILD_DIR)/boot/Core/Src/system_stm32f4xx.o $(BUILD_DIR)/boot/Core/Src/syscalls.o

.PHONY: all app app-slot-a app-slot-b bootloader clean size

all: app bootloader

app: app-slot-a

app-slot-a:
	$(MAKE) APP_LINKER=linker/app_slot_a.ld $(BUILD_DIR)/$(PROJECT)_app_slot_a.elf
	$(OBJCOPY) -O binary $(BUILD_DIR)/$(PROJECT)_app_slot_a.elf $(BUILD_DIR)/$(PROJECT)_app_slot_a.bin
	$(SIZE) $(BUILD_DIR)/$(PROJECT)_app_slot_a.elf

app-slot-b:
	$(MAKE) APP_LINKER=linker/app_slot_b.ld $(BUILD_DIR)/$(PROJECT)_app_slot_b.elf
	$(OBJCOPY) -O binary $(BUILD_DIR)/$(PROJECT)_app_slot_b.elf $(BUILD_DIR)/$(PROJECT)_app_slot_b.bin
	$(SIZE) $(BUILD_DIR)/$(PROJECT)_app_slot_b.elf

bootloader: $(BUILD_DIR)/$(PROJECT)_bootloader.elf
	$(OBJCOPY) -O binary $< $(BUILD_DIR)/$(PROJECT)_bootloader.bin
	$(SIZE) $<

$(BUILD_DIR)/$(PROJECT)_app_slot_a.elf: $(APP_OBJS) linker/app_slot_a.ld
	$(CC) $(APP_OBJS) -Tlinker/app_slot_a.ld $(LDFLAGS_COMMON) -Wl,-Map=$(BUILD_DIR)/$(PROJECT)_app_slot_a.map -o $@

$(BUILD_DIR)/$(PROJECT)_app_slot_b.elf: $(APP_OBJS) linker/app_slot_b.ld
	$(CC) $(APP_OBJS) -Tlinker/app_slot_b.ld $(LDFLAGS_COMMON) -Wl,-Map=$(BUILD_DIR)/$(PROJECT)_app_slot_b.map -o $@

$(BUILD_DIR)/$(PROJECT)_bootloader.elf: $(BOOT_OBJS) $(BOOT_LINKER)
	$(CC) $(BOOT_OBJS) -T$(BOOT_LINKER) $(LDFLAGS_COMMON) -Wl,-Map=$(BUILD_DIR)/$(PROJECT)_bootloader.map -o $@

$(BUILD_DIR)/app/%.o: %.c
	@if not exist "$(@D)" mkdir "$(@D)"
	$(CC) $(CFLAGS) $(FREERTOS_INCLUDES) -MMD -MP -c $< -o $@

$(BUILD_DIR)/app/%.o: %.s
	@if not exist "$(@D)" mkdir "$(@D)"
	$(CC) $(ASFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/boot/%.o: %.c
	@if not exist "$(@D)" mkdir "$(@D)"
	$(CC) $(CFLAGS) -DBOOTLOADER_BUILD -MMD -MP -c $< -o $@

$(BUILD_DIR)/boot/%.o: %.s
	@if not exist "$(@D)" mkdir "$(@D)"
	$(CC) $(ASFLAGS) -DBOOTLOADER_BUILD -MMD -MP -c $< -o $@

clean:
	@if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)"

-include $(APP_OBJS:.o=.d)
-include $(BOOT_OBJS:.o=.d)
