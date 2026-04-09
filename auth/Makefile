CC = gcc
CFLAGS = -Wall -Wextra -O2 -D_GNU_SOURCE $(shell pkg-config --cflags polkit-agent-1 glib-2.0 gio-unix-2.0 gtk+-3.0 gtk-layer-shell-0)
LDFLAGS = $(shell pkg-config --libs polkit-agent-1 glib-2.0 gio-unix-2.0 gtk+-3.0 gtk-layer-shell-0)

TARGET = auth
OBJS = auth.o auth_ui.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete!"

auth.o: auth.c
	$(CC) $(CFLAGS) -c auth.c -o auth.o

auth_ui.o: auth_ui.c
	$(CC) $(CFLAGS) -c auth_ui.c -o auth_ui.o

clean:
	@echo "Cleaning..."
	rm -f *.o $(TARGET)
	@echo "Clean complete!"

.PHONY: all clean
