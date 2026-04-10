#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include <glib/gstdio.h>
#include <time.h>
#include "control_center_ui.h"
#include "app_menu.h"
#include "battery_indicator.h"
#include "keyboard_layout.h"
#include "workspaces.h"
#include "sni_tray.h"
#include "resource_paths.h"
#include "volume_indicator.h"
#include "mic_indicator.h"
#include "wifi_indicator.h"
#include "sidebar_popup.h"
#include "notifications_ui.h"

static GtkWidget *time_label;
static const gint PANEL_HEIGHT = 32;
static guint recovery_source_id = 0;
static char *panel_executable_path = NULL;

static gboolean panel_has_available_monitor(void) {
    GdkDisplay *display = gdk_display_get_default();

    if (!display) return FALSE;
    if (gdk_display_get_primary_monitor(display)) return TRUE;
    return gdk_display_get_n_monitors(display) > 0;
}

static gboolean restart_panel_process(void) {
    GError *error = NULL;
    gchar *argv[] = { panel_executable_path, NULL };

    if (!panel_executable_path || panel_executable_path[0] == '\0') {
        g_warning("[Panel] Cannot restart: executable path is unavailable");
        return FALSE;
    }

    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        g_warning("[Panel] Failed to restart panel: %s", error ? error->message : "unknown error");
        if (error) g_error_free(error);
        return FALSE;
    }

    return TRUE;
}

static gboolean try_recover_panel(gpointer data) {
    (void)data;

    if (!panel_has_available_monitor()) return G_SOURCE_CONTINUE;

    if (restart_panel_process()) {
        recovery_source_id = 0;
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void on_panel_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;

    if (recovery_source_id == 0) {
        recovery_source_id = g_timeout_add(1000, try_recover_panel, NULL);
    }
}

static gboolean get_widget_monitor_geometry(GtkWidget *widget, GdkRectangle *geom) {
    if (!widget || !geom) return FALSE;

    GdkDisplay *display = gtk_widget_get_display(widget);
    if (!display) return FALSE;

    GdkMonitor *monitor = NULL;
    GdkWindow *gdk_win = gtk_widget_get_window(widget);
    if (gdk_win) {
        monitor = gdk_display_get_monitor_at_window(display, gdk_win);
    }
    if (!monitor) {
        monitor = gdk_display_get_primary_monitor(display);
    }
    if (!monitor && gdk_display_get_n_monitors(display) > 0) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    if (!monitor) return FALSE;

    gdk_monitor_get_geometry(monitor, geom);
    return TRUE;
}

static gboolean get_primary_monitor_geometry(GdkScreen *screen, GdkRectangle *geom) {
    if (!screen || !geom) return FALSE;

    GdkDisplay *display = gdk_screen_get_display(screen);
    if (!display) return FALSE;

    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    if (!monitor && gdk_display_get_n_monitors(display) > 0) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    if (!monitor) return FALSE;

    gdk_monitor_get_geometry(monitor, geom);
    return TRUE;
}

static void setup_wayfire_layer_shell(GtkWidget *window) {
    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_namespace(GTK_WINDOW(window), "con-panel");
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
    gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(window));
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, 0);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, 0);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
}

static gboolean update_time(gpointer data) {
    time_t rawtime;
    struct tm *info;
    char buffer[80];

    time(&rawtime);
    info = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%H:%M - %b %d", info);

    gtk_label_set_text(GTK_LABEL(time_label), buffer);
    return TRUE; // Continue repeating
}

static void on_panel_cc_toggle_clicked(GtkButton *btn, gpointer user_data) {
    GtkWidget *popup = GTK_WIDGET(user_data);
    if (popup) {
        if (GTK_IS_POPOVER(popup)) {
            if (gtk_widget_get_visible(popup)) {
                gtk_popover_popdown(GTK_POPOVER(popup));
            } else {
                gtk_popover_popup(GTK_POPOVER(popup));
            }
        } else {
            if (gtk_widget_get_visible(popup)) {
                gtk_widget_hide(popup);
            } else {
                gtk_widget_show_all(popup);
            }
        }
    }
}

static void on_panel_sidebar_toggle_clicked(GtkButton *btn, gpointer user_data) {
    GtkWidget *popup = GTK_WIDGET(user_data);
    sidebar_popup_toggle(popup, GTK_WIDGET(btn));
}

static void on_panel_screen_changed(GdkScreen *screen, gpointer user_data) {
    GtkWidget *window = GTK_WIDGET(user_data);
    (void)screen;

    GdkRectangle monitor_geom = {0};
    if (!get_widget_monitor_geometry(window, &monitor_geom)) return;

    gtk_widget_set_size_request(window, monitor_geom.width, PANEL_HEIGHT);
    gtk_window_resize(GTK_WINDOW(window), monitor_geom.width, PANEL_HEIGHT);
}

int main(int argc, char *argv[]) {
    g_setenv("NO_AT_BRIDGE", "1", TRUE);
    gtk_init(&argc, &argv);
    panel_executable_path = g_file_read_link("/proc/self/exe", NULL);
    if (!panel_executable_path && argc > 0 && argv[0] && argv[0][0] != '\0') {
        panel_executable_path = g_strdup(argv[0]);
    }

    // Initialize the background control center window (starts hidden)
    GtkWidget *cc_window = init_control_center();
    
    // Initialize the background app menu window (starts hidden)
    GtkWidget *app_menu_w = init_app_menu();

    // Initialize the background sidebar window (starts hidden)
    GtkWidget *sidebar_w = init_sidebar_popup();

    // Initialize the notifications popup (starts hidden)
    GtkWidget *notif_w = init_notifications_ui();

    // Create Panel Window
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DOCK);
    setup_wayfire_layer_shell(window);
    
    // Make window transparent
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual != NULL && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(window, visual);
        gtk_widget_set_app_paintable(window, TRUE);
    }
    
    // Set position and size
    GdkRectangle monitor_geom = {0};
    if (get_primary_monitor_geometry(screen, &monitor_geom)) {
        gtk_widget_set_size_request(window, monitor_geom.width, PANEL_HEIGHT);
        gtk_window_set_default_size(GTK_WINDOW(window), monitor_geom.width, PANEL_HEIGHT);
    }

    // Watch for screen changes
    g_signal_connect(screen, "size-changed", G_CALLBACK(on_panel_screen_changed), window);
    g_signal_connect(screen, "monitors-changed", G_CALLBACK(on_panel_screen_changed), window);

    // Layout
    GtkWidget *panel_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(panel_bar, "panel-bar");
    gtk_container_add(GTK_CONTAINER(window), panel_bar);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_name(hbox, "panel-content");
    gtk_widget_set_margin_start(hbox, 8);
    gtk_widget_set_margin_end(hbox, 8);
    gtk_box_pack_start(GTK_BOX(panel_bar), hbox, TRUE, TRUE, 0);

    // Left side
    GtkWidget *menu_btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(menu_btn), GTK_RELIEF_NONE);

    char *menu_icon_path = panel_resource_path_in("images", "vaxp.png");
    GdkPixbuf *menu_pixbuf = gdk_pixbuf_new_from_file_at_scale(menu_icon_path, 24, 24, TRUE, NULL);
    g_free(menu_icon_path);
    GtkWidget *menu_icon;
    if (menu_pixbuf) {
        menu_icon = gtk_image_new_from_pixbuf(menu_pixbuf);
        g_object_unref(menu_pixbuf);
    } else {
        menu_icon = gtk_image_new_from_icon_name("start-here-symbolic", GTK_ICON_SIZE_MENU);
    }
    gtk_container_add(GTK_CONTAINER(menu_btn), menu_icon);
    app_menu_set_relative_to(app_menu_w, menu_btn);
    g_signal_connect(menu_btn, "clicked", G_CALLBACK(on_panel_cc_toggle_clicked), app_menu_w);
    
    gtk_box_pack_start(GTK_BOX(hbox), menu_btn, FALSE, FALSE, 0);

    // Workspaces
    GtkWidget *workspaces_widget = create_workspaces_widget();
    gtk_widget_set_margin_start(workspaces_widget, 12);
    gtk_box_pack_start(GTK_BOX(hbox), workspaces_widget, FALSE, FALSE, 0);

    // Center (Time)
    GtkWidget *center_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(center_box, GTK_ALIGN_CENTER);
    time_label = gtk_label_new("");
    update_time(NULL);
    g_timeout_add_seconds(1, update_time, NULL);
    
    GtkWidget *time_btn = gtk_button_new();
    gtk_widget_set_name(time_btn, "time-button");
    gtk_button_set_relief(GTK_BUTTON(time_btn), GTK_RELIEF_NONE);
    gtk_container_add(GTK_CONTAINER(time_btn), time_label);
    sidebar_popup_set_relative_to(sidebar_w, time_btn);
    g_signal_connect(time_btn, "clicked", G_CALLBACK(on_panel_sidebar_toggle_clicked), sidebar_w);
    
    gtk_box_pack_start(GTK_BOX(center_box), time_btn, FALSE, FALSE, 0);
    
    gtk_box_set_center_widget(GTK_BOX(hbox), center_box);

    // Right side
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *cc_btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(cc_btn), GTK_RELIEF_NONE);
    GtkWidget *cc_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    char *cc_icon_path = panel_resource_path_in("images", "control-center-icon.svg");
    GdkPixbuf *cc_pixbuf = gdk_pixbuf_new_from_file_at_scale(cc_icon_path, 24, 24, TRUE, NULL);
    g_free(cc_icon_path);
    GtkWidget *cc_icon;
    if (cc_pixbuf) {
        cc_icon = gtk_image_new_from_pixbuf(cc_pixbuf);
        g_object_unref(cc_pixbuf);
    } else {
        // Fallback if image is missing
        cc_icon = gtk_image_new_from_icon_name("preferences-system-symbolic", GTK_ICON_SIZE_MENU);
    }
    
    gtk_box_pack_start(GTK_BOX(cc_box), cc_icon, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(cc_btn), cc_box);
    control_center_set_relative_to(cc_window, cc_btn);
    // SNI tray is intentionally created last so it starts after the rest of the panel UI.
    GtkWidget *sni_tray = create_sni_tray_widget();
    gtk_box_pack_start(GTK_BOX(right_box), sni_tray, FALSE, FALSE, 0);

    // Keyboard layout indicator
    GtkWidget *keyboard_layout_widget = create_keyboard_layout_widget();
    gtk_box_pack_start(GTK_BOX(right_box), keyboard_layout_widget, FALSE, FALSE, 0);

    // Status box to group wifi, mic, and volume
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(status_box, "status-box");

    // Wifi indicator
    GtkWidget *wifi_widget = create_wifi_indicator_widget();
    gtk_box_pack_start(GTK_BOX(status_box), wifi_widget, FALSE, FALSE, 0);

    // Mic indicator
    GtkWidget *mic_widget = create_mic_indicator_widget();
    gtk_box_pack_start(GTK_BOX(status_box), mic_widget, FALSE, FALSE, 0);

    // Volume indicator
    GtkWidget *volume_widget = create_volume_indicator_widget();
    gtk_box_pack_start(GTK_BOX(status_box), volume_widget, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(right_box), status_box, FALSE, FALSE, 0);


    // Sys box to group battery, search, and cc_btn
    GtkWidget *sys_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(sys_box, "sys-box");

    // Battery widget
    GtkWidget *battery_widget = get_battery_widget();
    gtk_box_pack_start(GTK_BOX(sys_box), battery_widget, FALSE, FALSE, 0);

    g_signal_connect(cc_btn, "clicked", G_CALLBACK(on_panel_cc_toggle_clicked), cc_window);
    
    // Search button (UI only)
    GtkWidget *search_btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(search_btn), GTK_RELIEF_NONE);
    GtkWidget *search_icon = gtk_image_new_from_icon_name("system-search-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(search_btn), search_icon);
    gtk_box_pack_start(GTK_BOX(sys_box), search_btn, FALSE, FALSE, 0);

    // Notifications bell button
    GtkWidget *notif_btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(notif_btn), GTK_RELIEF_NONE);
    GtkWidget *notif_icon = gtk_image_new_from_icon_name("preferences-system-notifications-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(notif_btn), notif_icon);
    g_signal_connect(notif_btn, "clicked", G_CALLBACK(on_panel_cc_toggle_clicked), notif_w);
    gtk_box_pack_start(GTK_BOX(sys_box), notif_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(sys_box), cc_btn, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(right_box), sys_box, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), right_box, FALSE, FALSE, 0);
    
    g_signal_connect(window, "destroy", G_CALLBACK(on_panel_window_destroy), NULL);
    
    gtk_widget_show_all(window);
    
    gtk_main();
    g_free(panel_executable_path);

    return 0;
}
