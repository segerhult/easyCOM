CC = gcc
CFLAGS = -Wall -Wextra -O2

# Directories
SRC_DIR = src/linux
BUILD_DIR = build
BUILD_DIR_LINUX = $(BUILD_DIR)/linux
BUILD_DIR_WIN = $(BUILD_DIR)/windows

# GTK Check
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0 2>/dev/null)

# Cross Compilation (Windows)
CC_WIN = x86_64-w64-mingw32-gcc

# Targets
TARGETS = $(BUILD_DIR_LINUX)/tcp_forwarder \
          $(BUILD_DIR_LINUX)/serial_forwarder \
          $(BUILD_DIR_LINUX)/push_client \
          $(BUILD_DIR_LINUX)/hub_server_cli

# Add GUI target if GTK is found
ifneq ($(GTK_CFLAGS),)
    TARGETS += $(BUILD_DIR_LINUX)/hub_server_gui
endif

all: $(BUILD_DIR_LINUX) $(TARGETS)

windows: $(BUILD_DIR_WIN)
	$(CC_WIN) $(CFLAGS) -o $(BUILD_DIR_WIN)/push_client.exe src/windows/client/push_client.c -lws2_32
	$(CC_WIN) $(CFLAGS) -o $(BUILD_DIR_WIN)/hub_server_cli.exe src/windows/server/hub_server_cli.c -lws2_32
	$(CC_WIN) $(CFLAGS) -mwindows -o $(BUILD_DIR_WIN)/hub_server_gui.exe src/windows/server/hub_server_gui.c -lws2_32 -luser32 -lgdi32
	$(CC_WIN) $(CFLAGS) -o $(BUILD_DIR_WIN)/VirtualComBridge.exe src/windows/driver/virtual_com_bridge.c -lws2_32

$(BUILD_DIR_LINUX):
	mkdir -p $(BUILD_DIR_LINUX)

$(BUILD_DIR_WIN):
	mkdir -p $(BUILD_DIR_WIN)

# Linux / macOS Targets
$(BUILD_DIR_LINUX)/tcp_forwarder: $(SRC_DIR)/legacy/tcp_forwarder.c
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR_LINUX)/serial_forwarder: $(SRC_DIR)/legacy/serial_forwarder.c
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR_LINUX)/push_client: $(SRC_DIR)/client/push_client.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread

$(BUILD_DIR_LINUX)/hub_server_cli: $(SRC_DIR)/server/hub_server_cli.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread

$(BUILD_DIR_LINUX)/hub_server_gui: $(SRC_DIR)/server/hub_server_gui.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS) -lpthread

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Available targets:"
	@echo "  all             : Build Linux/macOS tools in build/linux/"
	@echo "  windows         : Build Windows tools in build/windows/"
	@echo "  clean           : Remove build directory"
