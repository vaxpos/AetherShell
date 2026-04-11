#ifndef KB_INDICATOR_XORG_H
#define KB_INDICATOR_XORG_H

#include <gtk/gtk.h>
#include <X11/Xlib.h>

typedef struct {
    Display *xdisplay;
    int xkb_event_type;
    int current_group_index;
    char current_layout[16];
} XorgKeyboardState;

gboolean xorg_keyboard_init(XorgKeyboardState *state);
gboolean xorg_keyboard_refresh(XorgKeyboardState *state);

#endif
