.DEFAULT_GOAL := panel
.PHONY: panel clean install uninstall dist
CC      = gcc
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/panel
RESOURCES_DIR ?= $(DATADIR)/resources

PKGS = gtk+-3.0 gtk-layer-shell-0 libpulse libpulse-mainloop-glib \
       gio-2.0 json-glib-1.0 dbusmenu-glib-0.4
PKG_CHECK = $(shell pkg-config --exists $(PKGS) && echo yes || echo no)
ifeq ($(PKG_CHECK),no)
$(error Missing build dependencies. Install: libgtk-3-dev libgtk-layer-shell-dev libpulse-dev libjson-glib-dev libdbusmenu-glib-dev)
endif
CFLAGS  = -DVAXPWY_PANEL_RESOURCES_DIR=\"$(RESOURCES_DIR)\" \
          $(shell pkg-config --cflags $(PKGS))
LIBS    = $(shell pkg-config --libs $(PKGS))

SRC      = src/panel.c src/control_center_ui.c src/app_menu.c \
           src/battery_indicator.c src/pulse_volume.c src/power_profile.c \
           src/mpris_control.c src/power_actions.c src/venom_notifications.c \
           src/workspaces.c src/keyboard_layout.c src/sni_tray.c src/resource_paths.c \
           src/network_actions.c src/brightness_control.c src/volume_indicator.c \
           src/mic_indicator.c
INCLUDES = -Iinclude

panel: $(SRC)
	$(CC) -o panel $(SRC) $(CFLAGS) $(INCLUDES) $(LIBS)

clean:
	rm -f panel

install: panel
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(RESOURCES_DIR)
	install -d $(DESTDIR)$(RESOURCES_DIR)/images
	install -m 755 panel $(DESTDIR)$(BINDIR)/panel
	install -m 644 style.css $(DESTDIR)$(RESOURCES_DIR)/style.css
	install -m 644 images/vaxp.png $(DESTDIR)$(RESOURCES_DIR)/images/vaxp.png
	install -m 644 images/control-center-icon.svg $(DESTDIR)$(RESOURCES_DIR)/images/control-center-icon.svg
	install -m 644 images/control-center-icon.png $(DESTDIR)$(RESOURCES_DIR)/images/control-center-icon.png

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/panel
	rm -f $(DESTDIR)$(RESOURCES_DIR)/style.css
	rm -f $(DESTDIR)$(RESOURCES_DIR)/images/vaxp.png
	rm -f $(DESTDIR)$(RESOURCES_DIR)/images/control-center-icon.svg
	rm -f $(DESTDIR)$(RESOURCES_DIR)/images/control-center-icon.png
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(RESOURCES_DIR)/images
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(RESOURCES_DIR)
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(DATADIR)

dist:
	mkdir -p build/dist
	tar -czf build/dist/panel.tar.gz \
		Makefile style.css images include src
