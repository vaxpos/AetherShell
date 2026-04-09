CC = gcc
TARGET = AetherDock
BUILD_DIR = build
PROTO_DIR = $(BUILD_DIR)/proto
PROJECT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PROTO_XML = $(PROJECT_DIR)wlr-foreign-toplevel-management-unstable-v1.xml
PROTO_HEADER = $(PROTO_DIR)/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h
PROTO_CODE = $(PROTO_DIR)/wlr-foreign-toplevel-management-unstable-v1-client-protocol.c
PKGS = gtk+-3.0 gtk-layer-shell-0 wayland-client gio-unix-2.0

CFLAGS = -Wall -Wextra -O2 -Iinclude -Iinclude/logic -Iinclude/ui -I$(PROTO_DIR) $(shell pkg-config --cflags $(PKGS))
LIBS = $(shell pkg-config --libs $(PKGS))

# Logic Objects
LOGIC_OBJS = $(BUILD_DIR)/search_engine.o \
             $(BUILD_DIR)/app_manager.o

# Helper Objects (shared UI)
UI_HELPER_OBJS = $(BUILD_DIR)/launcher.o \
                 $(BUILD_DIR)/pager.o \
                 $(BUILD_DIR)/search.o \
                 $(BUILD_DIR)/trash.o \
                 $(BUILD_DIR)/drives.o \
                 $(BUILD_DIR)/utils.o

PANEL_OBJS = $(BUILD_DIR)/AetherDock.o
PROTO_OBJS = $(BUILD_DIR)/wlr-foreign-toplevel-management-unstable-v1-client-protocol.o

all: $(BUILD_DIR) $(PROTO_DIR) AetherDock

AetherDock: $(LOGIC_OBJS) $(UI_HELPER_OBJS) $(PANEL_OBJS) $(PROTO_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BUILD_DIR)/%.o: src/logic/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/ui/%.c $(PROTO_HEADER)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/wlr-foreign-toplevel-management-unstable-v1-client-protocol.o: $(PROTO_CODE)
	$(CC) $(CFLAGS) -c $< -o $@

$(PROTO_HEADER): $(PROTO_XML) | $(PROTO_DIR)
	wayland-scanner client-header $< $@

$(PROTO_CODE): $(PROTO_XML) | $(PROTO_DIR)
	wayland-scanner private-code $< $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PROTO_DIR):
	mkdir -p $(PROTO_DIR)

clean:
	rm -f $(TARGET)
	rm -rf $(BUILD_DIR)

.PHONY: all clean
