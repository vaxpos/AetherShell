#include "workspaces_xorg.h"
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <string.h>

static int get_x11_prop_int(Display *dpy, Window win, const char *prop_name) {
    Atom prop;
    Atom actual_type_return;
    int actual_format_return;
    unsigned long nitems_return;
    unsigned long bytes_after_return;
    unsigned char *prop_return = NULL;
    int status;
    int value = -1;

    if (!dpy || !prop_name) return -1;

    prop = XInternAtom(dpy, prop_name, True);
    if (prop == None) return -1;

    status = XGetWindowProperty(dpy, win, prop, 0, 1, False, AnyPropertyType,
                                &actual_type_return, &actual_format_return,
                                &nitems_return, &bytes_after_return, &prop_return);

    if (status == Success && prop_return) {
        if (actual_format_return == 32 && nitems_return > 0) {
            value = (int)(*(long *)prop_return);
        }
        XFree(prop_return);
    }

    return value;
}

gboolean xorg_workspaces_init(XorgWorkspaceState *state) {
    GdkDisplay *gdk_dpy;

    if (!state) return FALSE;
    memset(state, 0, sizeof(*state));

    gdk_dpy = gdk_display_get_default();
    if (!gdk_dpy || !GDK_IS_X11_DISPLAY(gdk_dpy)) return FALSE;

    state->dpy = gdk_x11_display_get_xdisplay(gdk_dpy);
    if (!state->dpy) return FALSE;

    state->root = DefaultRootWindow(state->dpy);
    state->num_desktops = get_x11_prop_int(state->dpy, state->root, "_NET_NUMBER_OF_DESKTOPS");
    state->current_desktop = get_x11_prop_int(state->dpy, state->root, "_NET_CURRENT_DESKTOP");

    if (state->num_desktops <= 0) state->num_desktops = 1;
    if (state->current_desktop < 0) state->current_desktop = 0;
    return TRUE;
}

gboolean xorg_workspaces_refresh(XorgWorkspaceState *state) {
    int num;
    int cur;

    if (!state || !state->dpy) return FALSE;

    num = get_x11_prop_int(state->dpy, state->root, "_NET_NUMBER_OF_DESKTOPS");
    cur = get_x11_prop_int(state->dpy, state->root, "_NET_CURRENT_DESKTOP");

    if (num <= 0) num = 1;
    if (cur < 0) cur = 0;

    if (num == state->num_desktops && cur == state->current_desktop) {
        return FALSE;
    }

    state->num_desktops = num;
    state->current_desktop = cur;
    return TRUE;
}

gboolean xorg_workspaces_switch(const XorgWorkspaceState *state, int desktop_idx) {
    XEvent xev;

    if (!state || !state->dpy || desktop_idx < 0) return FALSE;

    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = state->root;
    xev.xclient.message_type = XInternAtom(state->dpy, "_NET_CURRENT_DESKTOP", False);
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = desktop_idx;
    xev.xclient.data.l[1] = CurrentTime;

    XSendEvent(state->dpy, state->root, False,
               SubstructureNotifyMask | SubstructureRedirectMask, &xev);
    XFlush(state->dpy);
    return TRUE;
}
