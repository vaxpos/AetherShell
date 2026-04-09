#include "logic/pager_service.h"
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <gdk/gdkx.h>

static Display *x_display = NULL;
static Window root_window;
static Atom _net_number_of_desktops;
static Atom _net_current_desktop;
static Atom _net_client_list;
static Atom _net_client_list_stacking;
static Atom _net_wm_desktop;

void pager_svc_init(Display *dpy, Window root) {
    x_display = dpy;
    root_window = root;
    _net_number_of_desktops = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    _net_current_desktop = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    _net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    _net_client_list_stacking = XInternAtom(dpy, "_NET_CLIENT_LIST_STACKING", False);
    _net_wm_desktop = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
}

static long get_x11_property(Window win, Atom atom) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    long value = -1;

    if (XGetWindowProperty(x_display, win, atom, 0, 1, False,
                           XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
        if (prop && nitems > 0) {
            value = *(long *)prop;
        }
        if (prop) XFree(prop);
    }
    return value;
}

int pager_svc_get_current_desktop(void) {
    long val = get_x11_property(root_window, _net_current_desktop);
    return (val >= 0) ? (int)val : 0;
}

int pager_svc_get_num_desktops(void) {
    long val = get_x11_property(root_window, _net_number_of_desktops);
    return (val > 0) ? (int)val : 1;
}

void pager_svc_set_desktop(int index) {
    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = root_window;
    xev.xclient.message_type = _net_current_desktop;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = index;
    xev.xclient.data.l[1] = CurrentTime;
    
    XSendEvent(x_display, root_window, False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);
    XFlush(x_display);
}

GList *pager_svc_get_windows(int desktop_index) {
    GList *windows = NULL;
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    Atom list_atom = _net_client_list_stacking;
    if (XGetWindowProperty(x_display, root_window, list_atom, 0, 1024, False,
                           XA_WINDOW, &actual_type, &actual_format, &nitems, &bytes_after, &prop) != Success) {
        list_atom = _net_client_list;
        XGetWindowProperty(x_display, root_window, list_atom, 0, 1024, False,
                           XA_WINDOW, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
    }
    
    if (prop) {
        Window *list = (Window *)prop;
        for (unsigned long i = 0; i < nitems; i++) {
            Window win = list[i];
            long win_desktop = get_x11_property(win, _net_wm_desktop);
            if (win_desktop == desktop_index || win_desktop == 0xFFFFFFFF) {
                windows = g_list_append(windows, GINT_TO_POINTER(win));
            }
        }
        XFree(prop);
    }
    return windows;
}

Pixmap pager_svc_get_pixmap(Window win) {
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(x_display, win, &attrs)) return None;
    /* Allow fetching unmapped windows if compositor retains pixmap */
    /* if (attrs.map_state != IsViewable) return None; */
    
    gdk_x11_display_error_trap_push(gdk_display_get_default());
    
    Pixmap pixmap = XCompositeNameWindowPixmap(x_display, win);
    
    if (gdk_x11_display_error_trap_pop(gdk_display_get_default())) {
        return None;
    }
    
    return pixmap;
}

static GHashTable *snapshot_cache = NULL;

void pager_svc_clear_cache(void) {
    if (snapshot_cache) {
        g_hash_table_destroy(snapshot_cache);
        snapshot_cache = NULL;
    }
}

GdkPixbuf *pager_svc_get_snapshot_pixbuf(Window win, int width, int height) {
    if (!snapshot_cache) {
        snapshot_cache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);
    }
    
    /* Check cache first */
    GdkPixbuf *cached = g_hash_table_lookup(snapshot_cache, GINT_TO_POINTER((gint)win));
    if (cached) {
        return g_object_ref(cached);
    }

    XWindowAttributes attrs;
    if (!XGetWindowAttributes(x_display, win, &attrs)) return NULL;
    /* Only fetch viewable windows via CPU to avoid freezes */
    if (attrs.map_state != IsViewable) return NULL;
    
    gdk_x11_display_error_trap_push(gdk_display_get_default());
    
    GdkWindow *gdk_win = gdk_x11_window_foreign_new_for_display(gdk_display_get_default(), win);
    GdkPixbuf *full_size = NULL;
    
    if (gdk_win) {
         full_size = gdk_pixbuf_get_from_window(gdk_win, 0, 0, attrs.width, attrs.height);
    }
    
    gdk_x11_display_error_trap_pop_ignored(gdk_display_get_default());

    if (!full_size) {
        if (gdk_win) g_object_unref(gdk_win);
        return NULL;
    }
    
    GdkPixbuf *dest = gdk_pixbuf_scale_simple(full_size, width, height, GDK_INTERP_BILINEAR);
    g_object_unref(full_size);
    if (gdk_win) g_object_unref(gdk_win);
    
    if (dest) {
        /* Store in cache */
        g_hash_table_insert(snapshot_cache, GINT_TO_POINTER((gint)win), g_object_ref(dest));
    }
    
    return dest;
}

gboolean pager_svc_get_window_geometry(Window win, int *x, int *y, int *width, int *height) {
    XWindowAttributes attrs;
    if (XGetWindowAttributes(x_display, win, &attrs)) {
        if (x) *x = attrs.x;
        if (y) *y = attrs.y;
        if (width) *width = attrs.width;
        if (height) *height = attrs.height;
        return TRUE;
    }
    return FALSE;
}
