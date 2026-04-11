#include "window_backend.h"
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#endif

typedef struct {
    gboolean anchors[4];
    gint margins[4];
    gboolean auto_exclusive_zone;
    guint reposition_idle_id; /* pending g_idle_add for X11 repositioning */
} WindowBackendState;

typedef enum {
    PANEL_BACKEND_UNKNOWN = 0,
    PANEL_BACKEND_X11,
    PANEL_BACKEND_WAYLAND
} PanelBackendType;

static PanelBackendType detected_backend = PANEL_BACKEND_UNKNOWN;

static WindowBackendState *get_backend_state(GtkWindow *window) {
    WindowBackendState *state;

    if (!window) return NULL;

    state = g_object_get_data(G_OBJECT(window), "panel-window-backend-state");
    if (state) return state;

    state = g_new0(WindowBackendState, 1);
    g_object_set_data_full(G_OBJECT(window),
                           "panel-window-backend-state",
                           state,
                           g_free);
    return state;
}

void panel_window_backend_detect(void) {
    GdkDisplay *display = gdk_display_get_default();

    if (detected_backend != PANEL_BACKEND_UNKNOWN) return;
    if (!display) {
        detected_backend = PANEL_BACKEND_X11;
        return;
    }

#ifdef GDK_WINDOWING_WAYLAND
    detected_backend = GDK_IS_WAYLAND_DISPLAY(display)
        ? PANEL_BACKEND_WAYLAND
        : PANEL_BACKEND_X11;
#else
    detected_backend = PANEL_BACKEND_X11;
#endif
}

gboolean panel_window_backend_is_wayland(void) {
    panel_window_backend_detect();
    return detected_backend == PANEL_BACKEND_WAYLAND;
}

static void sync_x11_window_position(GtkWindow *window) {
    WindowBackendState *state;
    GtkWidget *widget;
    GdkDisplay *display;
    GdkMonitor *monitor = NULL;
    GdkRectangle geometry = {0};
    gint width = 0;
    gint height = 0;
    gint x = 0;
    gint y = 0;

    if (!window || panel_window_backend_is_wayland()) return;

    state = get_backend_state(window);
    if (!state) return;

    widget = GTK_WIDGET(window);
    display = gtk_widget_get_display(widget);
    if (!display) return;

    if (gtk_widget_get_window(widget)) {
        monitor = gdk_display_get_monitor_at_window(display, gtk_widget_get_window(widget));
    }
    if (!monitor) {
        monitor = gdk_display_get_primary_monitor(display);
    }
    if (!monitor && gdk_display_get_n_monitors(display) > 0) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    if (!monitor) return;

    gdk_monitor_get_geometry(monitor, &geometry);
    gtk_window_get_size(window, &width, &height);

    if (width <= 1 || height <= 1) {
        GtkAllocation alloc;
        gtk_widget_get_allocation(widget, &alloc);
        width = alloc.width;
        height = alloc.height;
    }

    if (state->anchors[GTK_LAYER_SHELL_EDGE_LEFT] &&
        state->anchors[GTK_LAYER_SHELL_EDGE_RIGHT]) {
        gint panel_width = geometry.width - state->margins[GTK_LAYER_SHELL_EDGE_LEFT] -
                           state->margins[GTK_LAYER_SHELL_EDGE_RIGHT];
        if (panel_width > 0) {
            gtk_window_resize(window, panel_width, MAX(height, 1));
            width = panel_width;
        }
        x = geometry.x + state->margins[GTK_LAYER_SHELL_EDGE_LEFT];
    } else if (state->anchors[GTK_LAYER_SHELL_EDGE_RIGHT]) {
        x = geometry.x + geometry.width - width - state->margins[GTK_LAYER_SHELL_EDGE_RIGHT];
    } else {
        x = geometry.x + state->margins[GTK_LAYER_SHELL_EDGE_LEFT];
    }

    if (state->anchors[GTK_LAYER_SHELL_EDGE_TOP] &&
        state->anchors[GTK_LAYER_SHELL_EDGE_BOTTOM]) {
        y = geometry.y + state->margins[GTK_LAYER_SHELL_EDGE_TOP];
    } else if (state->anchors[GTK_LAYER_SHELL_EDGE_BOTTOM]) {
        y = geometry.y + geometry.height - height - state->margins[GTK_LAYER_SHELL_EDGE_BOTTOM];
    } else {
        y = geometry.y + state->margins[GTK_LAYER_SHELL_EDGE_TOP];
    }

    gtk_window_move(window, x, y);
}

/* ─── Deferred single-shot repositioning ────────────────────────────── */

static gboolean reposition_idle_cb(gpointer data) {
    GtkWindow *window = GTK_WINDOW(data);
    WindowBackendState *state = get_backend_state(window);

    if (state) state->reposition_idle_id = 0;
    sync_x11_window_position(window);
    return G_SOURCE_REMOVE;
}

static void schedule_x11_reposition(GtkWindow *window) {
    WindowBackendState *state = get_backend_state(window);

    if (!state || state->reposition_idle_id) return;
    state->reposition_idle_id = g_idle_add(reposition_idle_cb, window);
}

/* Cancel pending idle on window destruction to prevent use-after-free. */
static void on_backend_window_destroy(GtkWidget *widget, gpointer user_data) {
    WindowBackendState *state;

    (void)user_data;

    state = get_backend_state(GTK_WINDOW(widget));
    if (state && state->reposition_idle_id) {
        g_source_remove(state->reposition_idle_id);
        state->reposition_idle_id = 0;
    }
}

/* ─── Panel bar: realize handler (standard WM-managed DOCK window) ───── */

static void on_backend_window_realize(GtkWidget *widget, gpointer user_data) {
    (void)user_data;

    if (panel_window_backend_is_wayland()) return;
    sync_x11_window_position(GTK_WINDOW(widget));
}

/* ─── Popup windows: override_redirect bypass + position sync ─────────
 *
 * override_redirect = True instructs the X11 server to skip the window
 * manager entirely for this window.  This means:
 *   • No WM decorations added (even if gtk_window_set_decorated is ignored)
 *   • No WM repositioning after gtk_window_move()
 *   • Window stacks above every normal & managed window automatically
 *
 * We apply it at realize time, before the window is mapped (made visible),
 * so the attribute is in effect for the very first XMapWindow call.
 *
 * Mouse input works without any extra work.  Keyboard input is obtained
 * naturally when the user clicks inside the popup (X11 sends Enter/focus
 * events to override_redirect windows on pointer-click).
 * ──────────────────────────────────────────────────────────────────── */

static void on_popup_window_realize(GtkWidget *widget, gpointer user_data) {
    (void)user_data;

    if (panel_window_backend_is_wayland()) return;

#ifdef GDK_WINDOWING_X11
    {
        GdkWindow *gdk_win = gtk_widget_get_window(widget);
        if (gdk_win && GDK_IS_X11_WINDOW(gdk_win)) {
            Display *xdpy  = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
            Window   xwin  = GDK_WINDOW_XID(gdk_win);
            XSetWindowAttributes attrs;

            attrs.override_redirect = True;
            XChangeWindowAttributes(xdpy, xwin, CWOverrideRedirect, &attrs);

            /* Flush to the X server so the attribute is applied before
             * GTK issues XMapWindow (which happens right after realize). */
            XFlush(xdpy);
        }
    }
#endif

    sync_x11_window_position(GTK_WINDOW(widget));
}

/* ─── init helpers ────────────────────────────────────────────────────── */

/* Used for the panel bar (DOCK). WM manages it normally. */
static void init_x11_window(GtkWindow *window) {
    GdkGeometry geo;

    gtk_window_set_skip_taskbar_hint(window, TRUE);
    gtk_window_set_skip_pager_hint(window, TRUE);
    gtk_window_set_keep_above(window, TRUE);
    gtk_window_stick(window);

    geo.min_width  = 0;
    geo.min_height = 0;
    gtk_window_set_geometry_hints(window, NULL, &geo, GDK_HINT_POS);

    g_signal_connect(window, "realize", G_CALLBACK(on_backend_window_realize), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(on_backend_window_destroy), NULL);
}

/* Used for ALL popup windows (control center, sidebar, app menu, …).
 * Sets override_redirect so the WM never touches position or decoration. */
static void init_x11_popup_window(GtkWindow *window) {
    GdkGeometry geo;

    /* Keep these hints as a courtesy to WMs that peek at override_redirect
     * windows (some compositors still read them). */
    gtk_window_set_skip_taskbar_hint(window, TRUE);
    gtk_window_set_skip_pager_hint(window, TRUE);
    gtk_window_set_keep_above(window, TRUE);
    gtk_window_stick(window);

    geo.min_width  = 0;
    geo.min_height = 0;
    gtk_window_set_geometry_hints(window, NULL, &geo, GDK_HINT_POS);

    /* on_popup_window_realize sets override_redirect BEFORE the window is
     * mapped, then calls sync_x11_window_position. */
    g_signal_connect(window, "realize", G_CALLBACK(on_popup_window_realize), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(on_backend_window_destroy), NULL);
}

/* ─── Public API ─────────────────────────────────────────────────────── */

void panel_window_backend_init_panel(GtkWindow *window, const char *namespace_name) {
    if (!window) return;

    if (panel_window_backend_is_wayland()) {
        gtk_layer_init_for_window(window);
        gtk_layer_set_namespace(window, namespace_name);
        gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    } else {
        /* Panel bar: standard DOCK window managed by the WM. */
        init_x11_window(window);
    }
}

void panel_window_backend_init_popup(GtkWindow *window,
                                     const char *namespace_name,
                                     GdkWindowTypeHint type_hint,
                                     GtkLayerShellKeyboardMode keyboard_mode) {
    if (!window) return;

    gtk_window_set_type_hint(window, type_hint);

    if (panel_window_backend_is_wayland()) {
        gtk_layer_init_for_window(window);
        gtk_layer_set_namespace(window, namespace_name);
        gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_keyboard_mode(window, keyboard_mode);
    } else {
        /* Popup windows: override_redirect — WM never touches them. */
        init_x11_popup_window(window);
    }
}

void panel_window_backend_set_anchor(GtkWindow *window,
                                     GtkLayerShellEdge edge,
                                     gboolean anchor_to_edge) {
    WindowBackendState *state;

    if (!window) return;

    state = get_backend_state(window);
    if (!state) return;
    state->anchors[edge] = anchor_to_edge;

    if (panel_window_backend_is_wayland()) {
        gtk_layer_set_anchor(window, edge, anchor_to_edge);
    } else {
        schedule_x11_reposition(window);
    }
}

void panel_window_backend_set_margin(GtkWindow *window,
                                     GtkLayerShellEdge edge,
                                     gint margin) {
    WindowBackendState *state;

    if (!window) return;

    state = get_backend_state(window);
    if (!state) return;
    state->margins[edge] = margin;

    if (panel_window_backend_is_wayland()) {
        gtk_layer_set_margin(window, edge, margin);
    } else {
        schedule_x11_reposition(window);
    }
}

void panel_window_backend_auto_exclusive_zone_enable(GtkWindow *window) {
    WindowBackendState *state;

    if (!window) return;

    state = get_backend_state(window);
    if (!state) return;
    state->auto_exclusive_zone = TRUE;

    if (panel_window_backend_is_wayland()) {
        gtk_layer_auto_exclusive_zone_enable(window);
    }
}
