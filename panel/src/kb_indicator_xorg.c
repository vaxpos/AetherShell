#include "kb_indicator_xorg.h"
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void to_upper_str(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = (char)toupper((unsigned char)str[i]);
    }
}

static void get_group_label(Display *xdisplay, int group, char *out, size_t out_size) {
    XkbDescPtr desc;

    out[0] = '\0';
    if (!xdisplay) return;

    desc = XkbAllocKeyboard();
    if (!desc) {
        snprintf(out, out_size, "G%d", group + 1);
        return;
    }

    if (XkbGetNames(xdisplay, XkbGroupNamesMask, desc) != Success || !desc->names) {
        XkbFreeKeyboard(desc, 0, True);
        snprintf(out, out_size, "G%d", group + 1);
        return;
    }

    if (group >= 0 && group < XkbNumKbdGroups && desc->names->groups[group] != None) {
        char *full = XGetAtomName(xdisplay, desc->names->groups[group]);

        if (full && full[0] != '\0') {
            char *open = strrchr(full, '(');
            char *close = strrchr(full, ')');
            const char *src;
            size_t len;

            if (open && close && close > open + 1) {
                src = open + 1;
                len = (size_t)(close - open - 1);
            } else {
                src = full;
                len = strlen(full);
            }

            if (len >= out_size) len = out_size - 1;
            memcpy(out, src, len);
            out[len] = '\0';
            to_upper_str(out);
            XFree(full);
        } else {
            if (full) XFree(full);
            snprintf(out, out_size, "G%d", group + 1);
        }
    } else {
        snprintf(out, out_size, "G%d", group + 1);
    }

    XkbFreeKeyboard(desc, 0, True);
}

gboolean xorg_keyboard_init(XorgKeyboardState *state) {
    GdkDisplay *gdk_display;
    int opcode;
    int error;
    int major;
    int minor;
    XkbStateRec xkb_state;

    if (!state) return FALSE;
    memset(state, 0, sizeof(*state));
    state->current_group_index = -1;

    gdk_display = gdk_display_get_default();
    if (!gdk_display || !GDK_IS_X11_DISPLAY(gdk_display)) return FALSE;

    state->xdisplay = gdk_x11_display_get_xdisplay(gdk_display);
    if (!state->xdisplay) return FALSE;

    if (!XkbQueryExtension(state->xdisplay, &opcode, &state->xkb_event_type, &error, &major, &minor)) {
        return FALSE;
    }

    if (XkbGetState(state->xdisplay, XkbUseCoreKbd, &xkb_state) == Success) {
        state->current_group_index = xkb_state.locked_group;
        get_group_label(state->xdisplay, state->current_group_index,
                        state->current_layout, sizeof(state->current_layout));
    }

    return TRUE;
}

gboolean xorg_keyboard_refresh(XorgKeyboardState *state) {
    XkbStateRec xkb_state;
    char layout[16] = {0};

    if (!state || !state->xdisplay) return FALSE;

    if (XkbGetState(state->xdisplay, XkbUseCoreKbd, &xkb_state) != Success) {
        return FALSE;
    }

    get_group_label(state->xdisplay, xkb_state.locked_group, layout, sizeof(layout));

    if (state->current_group_index == xkb_state.locked_group &&
        strcmp(state->current_layout, layout) == 0) {
        return FALSE;
    }

    state->current_group_index = xkb_state.locked_group;
    g_strlcpy(state->current_layout, layout, sizeof(state->current_layout));
    return TRUE;
}
