#include "osd_protocols_x11.h"
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <ctype.h>
#include <string.h>

static Display *xdisplay = NULL;
static int xkb_event_type = 0;
static int current_group_index = -1;
static guint x11_watch_id = 0;
static OsdProtocolCallbacks callbacks = {0};

static gboolean is_x11_session(void) {
    const char *display = g_getenv("DISPLAY");
    if (!display || !display[0]) {
        return FALSE;
    }
    return TRUE;
}

static void xkb_get_group_label(int group, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';

    XkbDescPtr desc = XkbAllocKeyboard();
    if (!desc) {
        if (out_size > 3) {
            out[0] = 'G';
            out[1] = '0' + (group + 1);
            out[2] = '\0';
        }
        return;
    }

    if (XkbGetNames(xdisplay, XkbGroupNamesMask, desc) != Success || !desc->names) {
        XkbFreeKeyboard(desc, 0, True);
        if (out_size > 3) {
            out[0] = 'G';
            out[1] = '0' + (group + 1);
            out[2] = '\0';
        }
        return;
    }

    if (group >= 0 && group < XkbNumKbdGroups &&
        desc->names->groups[group] != None) {

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

            for (size_t i = 0; out[i]; i++) {
                out[i] = (char)toupper((unsigned char)out[i]);
            }

            XFree(full);
        } else {
            if (full) XFree(full);
            if (out_size > 3) {
                out[0] = 'G';
                out[1] = '0' + (group + 1);
                out[2] = '\0';
            }
        }
    } else if (out_size > 3) {
        out[0] = 'G';
        out[1] = '0' + (group + 1);
        out[2] = '\0';
    }

    XkbFreeKeyboard(desc, 0, True);
}

static void apply_group_label(int group) {
    if (!callbacks.set_keyboard_label) return;

    char label[16];
    xkb_get_group_label(group, label, sizeof(label));
    if (label[0] == '\0') return;
    callbacks.set_keyboard_label(label, TRUE);
}

static gboolean on_x11_event(GIOChannel *source, GIOCondition condition, gpointer data) {
    (void)source;
    (void)data;

    if (!xdisplay) return G_SOURCE_REMOVE;
    if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) return G_SOURCE_REMOVE;

    while (XPending(xdisplay) > 0) {
        XEvent xev;
        XNextEvent(xdisplay, &xev);

        if (xev.type == xkb_event_type) {
            XkbEvent *xkb = (XkbEvent *)&xev;
            if (xkb->any.xkb_type == XkbStateNotify) {
                if (!(xkb->state.changed & XkbGroupStateMask)) continue;

                XkbStateRec fresh;
                if (XkbGetState(xdisplay, XkbUseCoreKbd, &fresh) != Success) continue;

                int new_group = fresh.locked_group;
                if (new_group == current_group_index) continue;
                current_group_index = new_group;
                apply_group_label(current_group_index);
            }
        }
    }

    return G_SOURCE_CONTINUE;
}

gboolean osd_protocols_x11_init(const OsdProtocolCallbacks *cb) {
    if (!cb || !cb->set_keyboard_label) return FALSE;
    callbacks = *cb;

    if (!is_x11_session()) return FALSE;

    xdisplay = XOpenDisplay(NULL);
    if (!xdisplay) return FALSE;

    int opcode, error, major = XkbMajorVersion, minor = XkbMinorVersion;
    if (!XkbQueryExtension(xdisplay, &opcode, &xkb_event_type, &error, &major, &minor)) {
        XCloseDisplay(xdisplay);
        xdisplay = NULL;
        return FALSE;
    }

    XkbSelectEvents(xdisplay, XkbUseCoreKbd,
                    XkbStateNotifyMask, XkbStateNotifyMask);

    XkbStateRec state;
    if (XkbGetState(xdisplay, XkbUseCoreKbd, &state) == Success) {
        current_group_index = state.locked_group;
        apply_group_label(current_group_index);
    }

    int fd = ConnectionNumber(xdisplay);
    if (fd >= 0) {
        GIOChannel *channel = g_io_channel_unix_new(fd);
        g_io_channel_set_encoding(channel, NULL, NULL);
        g_io_channel_set_buffered(channel, FALSE);
        x11_watch_id = g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                      on_x11_event, NULL);
        g_io_channel_unref(channel);
    }

    return TRUE;
}

void osd_protocols_x11_shutdown(void) {
    if (x11_watch_id) {
        g_source_remove(x11_watch_id);
        x11_watch_id = 0;
    }
    if (xdisplay) {
        XCloseDisplay(xdisplay);
        xdisplay = NULL;
    }
}
