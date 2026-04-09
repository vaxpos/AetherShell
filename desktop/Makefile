CC      = gcc
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
APPDIR  ?= $(PREFIX)/share/applications
CFLAGS  = $(shell pkg-config --cflags gtk+-3.0 gio-2.0 gtk-layer-shell-0) -Iinclude -Wall -Wextra -O2
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 gio-2.0 gtk-layer-shell-0) -lm -ldl

SRCDIR  = src
OBJDIR  = obj
INCDIR  = include

# ---- Main desktop manager ----
TARGET  = desktop
MAIN_SRCS = \
	$(SRCDIR)/desktop.c \
	$(SRCDIR)/desktop_config.c \
	$(SRCDIR)/wallpaper.c \
	$(SRCDIR)/filesystem.c \
	$(SRCDIR)/icons.c \
	$(SRCDIR)/menu.c \
	$(SRCDIR)/selection.c \
	$(SRCDIR)/widgets_manager.c

# ---- setbg tool ----
SETBG_TARGET = vaxp-setbg
SETBG_SRC    = $(SRCDIR)/vaxp-setbg.c
SETBG_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 2>/dev/null) -Iinclude -Wall -O2
SETBG_LDFLAGS = $(shell pkg-config --libs gtk+-3.0 2>/dev/null) -lImlib2 -lX11

BIN_TARGETS = $(TARGET)
ifneq ($(wildcard $(SETBG_SRC)),)
BIN_TARGETS += $(SETBG_TARGET)
endif

all: $(OBJDIR) $(BIN_TARGETS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Build desktop manager from the modular sources
$(TARGET): $(MAIN_SRCS)
	$(CC) -o $@ $(MAIN_SRCS) $(CFLAGS) $(LDFLAGS)

# Build setbg tool
$(SETBG_TARGET): $(SETBG_SRC)
	$(CC) -o $@ $< $(SETBG_CFLAGS) $(SETBG_LDFLAGS)

# ---- Widgets Compilation ----
WIDGETS_DIR_SRC = src/widgets
WIDGET__OUT_DIR = obj/widgets
CONFIG_WIDGET_DIR = $(HOME)/.config/vaxp/widgets

widgets: $(WIDGET__OUT_DIR)
	@mkdir -p $(CONFIG_WIDGET_DIR)
	@for src in $(WIDGETS_DIR_SRC)/*.c; do \
		if [ -f "$$src" ]; then \
			filename=$$(basename -- "$$src"); \
			name="$${filename%.*}"; \
			echo "Compiling widget: $$name.so"; \
			$(CC) -shared -fPIC -o $(CONFIG_WIDGET_DIR)/$$name.so $$src $(CFLAGS) $(LDFLAGS) ; \
		fi \
	done

$(WIDGET__OUT_DIR):
	mkdir -p $(WIDGET__OUT_DIR)

clean:
	rm -f $(TARGET) $(SETBG_TARGET)
	rm -f $(OBJDIR)/*.o
	rm -f $(CONFIG_WIDGET_DIR)/*.so

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(APPDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -m 644 desktop.desktop $(DESTDIR)$(APPDIR)/desktop.desktop

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(APPDIR)/desktop.desktop

.PHONY: all clean widgets install uninstall
