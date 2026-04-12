.DEFAULT_GOAL := all
CC      = gcc
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
USER_UNIT_DIR ?= $(PREFIX)/lib/systemd/user


CFLAGS  = -Wall -Wextra -O2 -Isrc -Iinclude \
          $(shell pkg-config --cflags gtk+-3.0 glib-2.0 gio-2.0 gio-unix-2.0 \
            json-glib-1.0 xkbregistry gtk-layer-shell-0 wayland-client \
            libpulse libpulse-mainloop-glib x11) -g
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 glib-2.0 gio-2.0 gio-unix-2.0 \
            json-glib-1.0 xkbregistry gtk-layer-shell-0 wayland-client \
            libpulse libpulse-mainloop-glib x11) -lm -lpthread

SRC_DIR = src
OBJ_DIR = build
BIN     = osd-notify
SERVICE = osd-notify.service

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN)

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

install-service:
	install -d $(DESTDIR)$(USER_UNIT_DIR)
	install -m 644 $(SERVICE) $(DESTDIR)$(USER_UNIT_DIR)/$(SERVICE)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

uninstall-service:
	rm -f $(DESTDIR)$(USER_UNIT_DIR)/$(SERVICE)

dist:
	mkdir -p $(OBJ_DIR)/dist
	tar -czf $(OBJ_DIR)/dist/$(BIN).tar.gz \
		Makefile $(SERVICE) src

.PHONY: all clean install install-service uninstall uninstall-service dist
