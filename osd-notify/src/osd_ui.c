#include "osd_ui.h"
#include <gtk-layer-shell.h>
#if defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#include <wayland-client.h>
#endif

#define OSD_WIDTH 200
#define OSD_HEIGHT 200

static gboolean is_wayland_session(void) {
#if defined(GDK_WINDOWING_WAYLAND)
    return GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default());
#else
    return FALSE;
#endif
}

static gboolean hide_osd_cb(gpointer data) {
    OsdUi *ui = (OsdUi *)data;
    if (!ui || !ui->window) return G_SOURCE_REMOVE;
    gtk_widget_hide(ui->window);
    ui->hide_timer_id = 0;
    return G_SOURCE_REMOVE;
}

void osd_ui_apply_input_passthrough(OsdUi *ui) {
    if (!ui || !ui->window) return;
    GdkWindow *gdk_win = gtk_widget_get_window(ui->window);
    if (!gdk_win) return;

    gdk_window_set_pass_through(gdk_win, TRUE);

    cairo_region_t *empty = cairo_region_create();
    gdk_window_input_shape_combine_region(gdk_win, empty, 0, 0);
    cairo_region_destroy(empty);

#if defined(GDK_WINDOWING_WAYLAND)
    if (is_wayland_session() && GDK_IS_WAYLAND_WINDOW(gdk_win)) {
        GdkDisplay *display = gdk_display_get_default();
        struct wl_compositor *compositor = display ? gdk_wayland_display_get_wl_compositor(display) : NULL;
        struct wl_surface *surface = gdk_wayland_window_get_wl_surface(gdk_win);
        if (compositor && surface) {
            struct wl_region *region = wl_compositor_create_region(compositor);
            wl_surface_set_input_region(surface, region);
            wl_surface_commit(surface);
            wl_region_destroy(region);
        }
    }
#endif
}

void osd_ui_update_position(OsdUi *ui, int width, int height) {
    if (!ui || !ui->window) return;

    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = display ? gdk_display_get_primary_monitor(display) : NULL;
    if (!monitor) return;

    GdkRectangle workarea;
    gdk_monitor_get_workarea(monitor, &workarea);

    int x = workarea.x + (workarea.width - width) / 2;
    int y = workarea.y + (workarea.height - height) / 2;

    if (ui->use_layer_shell) {
        gtk_layer_set_anchor(GTK_WINDOW(ui->window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(ui->window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(ui->window), GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
        gtk_layer_set_anchor(GTK_WINDOW(ui->window), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
        gtk_layer_set_margin(GTK_WINDOW(ui->window), GTK_LAYER_SHELL_EDGE_LEFT, x);
        gtk_layer_set_margin(GTK_WINDOW(ui->window), GTK_LAYER_SHELL_EDGE_TOP, y);
        gtk_layer_set_margin(GTK_WINDOW(ui->window), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
        gtk_layer_set_margin(GTK_WINDOW(ui->window), GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
        gtk_layer_set_monitor(GTK_WINDOW(ui->window), monitor);
    } else {
        gtk_window_move(GTK_WINDOW(ui->window), x, y);
    }
}

void osd_ui_init(OsdUi *ui, GCallback draw_cb, gpointer draw_data) {
    if (!ui) return;

    ui->window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_default_size(GTK_WINDOW(ui->window), OSD_WIDTH, OSD_HEIGHT);
    gtk_window_set_decorated(GTK_WINDOW(ui->window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(ui->window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(ui->window), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(ui->window), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(ui->window), FALSE);
    gtk_widget_set_app_paintable(ui->window, TRUE);

    ui->use_layer_shell = is_wayland_session() && gtk_layer_is_supported();
    if (ui->use_layer_shell) {
        gtk_layer_init_for_window(GTK_WINDOW(ui->window));
        gtk_layer_set_namespace(GTK_WINDOW(ui->window), "venom-osd");
        gtk_layer_set_layer(GTK_WINDOW(ui->window), GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(ui->window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_set_exclusive_zone(GTK_WINDOW(ui->window), 0);
    } else {
        gtk_window_set_position(GTK_WINDOW(ui->window), GTK_WIN_POS_CENTER);
    }

    GdkScreen *screen = gtk_widget_get_screen(ui->window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) {
        gtk_widget_set_visual(ui->window, visual);
    }

    ui->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(ui->drawing_area, OSD_WIDTH, OSD_HEIGHT);
    if (draw_cb) {
        g_signal_connect(ui->drawing_area, "draw", draw_cb, draw_data);
    }
    gtk_container_add(GTK_CONTAINER(ui->window), ui->drawing_area);

    gtk_widget_realize(ui->window);
    osd_ui_apply_input_passthrough(ui);
}

void osd_ui_queue_draw(OsdUi *ui) {
    if (!ui || !ui->drawing_area) return;
    gtk_widget_queue_draw(ui->drawing_area);
}

void osd_ui_show(OsdUi *ui, int width, int height, guint duration_ms) {
    if (!ui || !ui->window || !ui->drawing_area) return;

    if (ui->hide_timer_id) {
        g_source_remove(ui->hide_timer_id);
        ui->hide_timer_id = 0;
    }

    gtk_widget_set_size_request(ui->drawing_area, width, height);
    gtk_window_resize(GTK_WINDOW(ui->window), width, height);
    osd_ui_update_position(ui, width, height);

    osd_ui_queue_draw(ui);
    gtk_widget_show_all(ui->window);
    osd_ui_apply_input_passthrough(ui);

    ui->hide_timer_id = g_timeout_add(duration_ms, hide_osd_cb, ui);
}
