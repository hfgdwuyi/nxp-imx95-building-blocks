# Building Blocks - Makefile for i.MX95
# Target: aarch64 (i.MX95 EVK) or native for development
#
# External apps link individual .o files, e.g.:
#   aarch64-linux-gnu-gcc -Ilibbb -o my_app my_app.c \
#       build/obj/bb_hal_led.o -static -lpthread

CC       ?= gcc
CFLAGS   := -std=c11 -Wall -Wextra -Os -D_GNU_SOURCE \
            -Ilibbb -Ihal -Imiddleware -Iservices -Itools/bb-update
LDFLAGS  := -static -lpthread

BUILD_DIR := build
BIN_DIR   := $(BUILD_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/obj

# VPATH: search source files in these directories
VPATH := libbb:hal:middleware:services:tools:tools/bb-update:blocks/bb-led

# ---- Individual module objects (link what you need) ----
OBJ_bb_block    := $(OBJ_DIR)/bb_block.o
OBJ_bb_bus      := $(OBJ_DIR)/bb_bus.o
OBJ_bb_json     := $(OBJ_DIR)/bb_json.o
OBJ_bb_thread   := $(OBJ_DIR)/bb_thread.o
OBJ_bb_pool     := $(OBJ_DIR)/bb_pool.o
OBJ_bb_log      := $(OBJ_DIR)/bb_log.o
OBJ_bb_persist  := $(OBJ_DIR)/bb_persist.o
OBJ_bb_recovery := $(OBJ_DIR)/bb_recovery.o
OBJ_bb_hal_led  := $(OBJ_DIR)/bb_hal_led.o
OBJ_bb_hal_gpio := $(OBJ_DIR)/bb_hal_gpio.o
OBJ_bb_hal_i2c  := $(OBJ_DIR)/bb_hal_i2c.o
OBJ_bb_hal_spi  := $(OBJ_DIR)/bb_hal_spi.o
OBJ_bb_hal_pwm  := $(OBJ_DIR)/bb_hal_pwm.o
OBJ_bb_hal_rtc  := $(OBJ_DIR)/bb_hal_rtc.o
OBJ_bb_hal_wdg  := $(OBJ_DIR)/bb_hal_wdg.o
OBJ_bb_hal_uart := $(OBJ_DIR)/bb_hal_uart.o
OBJ_bb_update   := $(OBJ_DIR)/bb_update.o
OBJ_bb_hal_display  := $(OBJ_DIR)/bb_hal_display.o
OBJ_bb_hal_audio    := $(OBJ_DIR)/bb_hal_audio.o
OBJ_bb_audio_stream := $(OBJ_DIR)/bb_audio_stream.o

# All libbb objects (for deploy)
LIBBB_OBJS := $(OBJ_bb_block) $(OBJ_bb_bus) $(OBJ_bb_json) \
              $(OBJ_bb_thread) $(OBJ_bb_pool) $(OBJ_bb_log) \
              $(OBJ_bb_persist) $(OBJ_bb_recovery) \
              $(OBJ_bb_hal_led) $(OBJ_bb_hal_gpio) $(OBJ_bb_hal_i2c) $(OBJ_bb_hal_spi) \
              $(OBJ_bb_hal_pwm) $(OBJ_bb_hal_rtc) $(OBJ_bb_hal_wdg) $(OBJ_bb_hal_uart) \
              $(OBJ_bb_hal_display) $(OBJ_bb_hal_audio) $(OBJ_bb_audio_stream)

# ---- Targets ----
TARGETS := $(BIN_DIR)/bb-busd $(BIN_DIR)/bb-led $(BIN_DIR)/bb-cli $(BIN_DIR)/bb-hal-test \
           $(BIN_DIR)/bb-update \
           $(BIN_DIR)/bb-display-test \
           $(BIN_DIR)/bb-audio-test \
           $(BIN_DIR)/bb-audio-loopback

.PHONY: all clean deploy cross bbu

all: $(TARGETS)
	@echo "Build complete:"
	@ls -lh $(BIN_DIR)/

# ---- Bus daemon (standalone, no libbb) ----
$(BIN_DIR)/bb-busd: services/bb-busd.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# ---- LED block: links block+bus+json+log+hal_led ----
$(BIN_DIR)/bb-led: blocks/bb-led/main.c $(OBJ_bb_block) $(OBJ_bb_bus) $(OBJ_bb_json) $(OBJ_bb_log) $(OBJ_bb_hal_led) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ---- CLI tool: json+persist (for system queries) ----
$(BIN_DIR)/bb-cli: tools/bb-cli.c $(OBJ_bb_json) $(OBJ_bb_persist) $(OBJ_bb_log) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ---- HAL test tool: needs all HAL modules ----
$(BIN_DIR)/bb-hal-test: tools/bb-hal-test.c $(OBJ_bb_hal_i2c) $(OBJ_bb_hal_spi) $(OBJ_bb_hal_gpio) $(OBJ_bb_hal_led) $(OBJ_bb_hal_pwm) $(OBJ_bb_hal_rtc) $(OBJ_bb_hal_wdg) $(OBJ_bb_hal_uart) $(OBJ_bb_log) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ---- Update tool: update logic + persist + recovery ----
$(BIN_DIR)/bb-update: tools/bb-update/main.c $(OBJ_bb_update) $(OBJ_bb_persist) $(OBJ_bb_recovery) $(OBJ_bb_json) $(OBJ_bb_log) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ---- Display test: needs display HAL only ----
$(BIN_DIR)/bb-display-test: tools/bb-display-test.c $(OBJ_bb_hal_display) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

# ---- Audio test: needs audio HAL only ----
$(BIN_DIR)/bb-audio-test: tools/bb-audio-test.c $(OBJ_bb_hal_audio) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

# ---- Audio loopback: needs stream + HAL + pool + thread + log ----
$(BIN_DIR)/bb-audio-loopback: tools/bb-audio-loopback.c $(OBJ_bb_audio_stream) $(OBJ_bb_hal_audio) $(OBJ_bb_pool) $(OBJ_bb_thread) $(OBJ_bb_log) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

# ---- Compile individual library modules (searched via VPATH) ----
$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- Directories ----
$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

# ---- Deploy to i.MX95 board ----
deploy: all
	@echo "Deploying to $(TARGET_HOST)..."
	ssh -o StrictHostKeyChecking=no root@$(TARGET_HOST) \
		"mkdir -p /opt/building-blocks/bin /opt/building-blocks/include /opt/building-blocks/obj"
	scp -o StrictHostKeyChecking=no \
		$(BIN_DIR)/* root@$(TARGET_HOST):/opt/building-blocks/bin/
	scp -o StrictHostKeyChecking=no \
		$(LIBBB_OBJS) root@$(TARGET_HOST):/opt/building-blocks/obj/
	scp -o StrictHostKeyChecking=no \
		libbb/*.h hal/*.h middleware/*.h root@$(TARGET_HOST):/opt/building-blocks/include/
	scp -o StrictHostKeyChecking=no \
		deploy/*.service root@$(TARGET_HOST):/etc/systemd/system/
	scp -o StrictHostKeyChecking=no \
		deploy/system/*.service deploy/system/*.timer root@$(TARGET_HOST):/etc/systemd/system/
	scp -o StrictHostKeyChecking=no \
		deploy/boot/boot.cmd root@$(TARGET_HOST):/opt/building-blocks/boot/
	ssh -o StrictHostKeyChecking=no root@$(TARGET_HOST) \
		"chmod +x /opt/building-blocks/bin/* \
		 && ln -sf /opt/building-blocks/bin/bb-cli /usr/bin/bb-cli \
		 && ln -sf /opt/building-blocks/bin/bb-busd /usr/bin/bb-busd \
		 && ln -sf /opt/building-blocks/bin/bb-update /usr/bin/bb-update \
		 && systemctl daemon-reload \
		 && systemctl enable bb-busd bb-led bb-boot-ok bb-health bb-logrotate.timer bb-time-sync bb-update-check.timer \
		 && systemctl restart bb-busd bb-led"
	@echo "Deploy complete."

# Cross-compile for aarch64
cross: CC = aarch64-linux-gnu-gcc
cross:
	@$(MAKE) CC=$(CC) CFLAGS="$(CFLAGS) -DBOARD_NXP_IMX95_EVK" all

# Cross-compile for aarch64 (alias)
cross-evk: cross

# Create a .bbu update package (runs on host)
bbu: all
	@echo "Creating update package..."
	./build/bin/bb-update create \
		--version "2.0.0" \
		--slot "=" \
		--product "i.MX95 EVK" \
		--output build/update.bbu

clean:
	rm -rf $(BUILD_DIR)
