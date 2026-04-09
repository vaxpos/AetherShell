/*
 * desktop.c
 * UI bootstrap and application entry point.
 */

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <malloc.h>
#include "icons.h"
#include "selection.h"
#include "wallpaper.h"
#include "widgets_manager.h"

static guint recovery_source_id = 0;
static char *desktop_executable_path = NULL;

static void on_main_window_destroy(GtkWidget *widget, gpointer user_data);
static gboolean try_recover_desktop(gpointer user_data);
static void build_desktop_ui(void);
static gboolean restart_desktop_process(void);

static void setup_drag_dest(void) {
    GtkTargetEntry targets[] = { { "text/uri-list", 0, 0 } };

    gtk_drag_dest_set(icon_layout, GTK_DEST_DEFAULT_ALL, targets, 1,
                      GDK_ACTION_MOVE | GDK_ACTION_COPY);
    g_signal_connect(icon_layout, "drag-data-received",
                     G_CALLBACK(on_bg_drag_data_received), NULL);
}

static void setup_layout_events(void) {
    g_signal_connect_after(icon_layout, "draw", G_CALLBACK(on_layout_draw_fg), NULL);
    g_signal_connect(icon_layout, "button-press-event", G_CALLBACK(on_bg_button_press), NULL);
    g_signal_connect(icon_layout, "motion-notify-event", G_CALLBACK(on_bg_motion), NULL);
    g_signal_connect(icon_layout, "button-release-event", G_CALLBACK(on_bg_button_release), NULL);
}

static void apply_desktop_css(void) {
    GdkScreen *screen = gtk_widget_get_screen(main_window);
    GtkCssProvider *css = gtk_css_provider_new();

    gtk_css_provider_load_from_data(css,
        "window { background-color: transparent; }"
        "#desktop-item { background: transparent; border-radius: 5px; padding: 8px; transition: all 0.1s; }"
        "#desktop-item:hover { background: rgba(0, 0, 0, 0); }"
        "#desktop-item.selected { background: rgba(52, 152, 219, 0.4); border: 1px solid rgba(52, 152, 219, 0.8); }"
        "label { color: white; text-shadow: 1px 1px 2px black; font-weight: bold; }"
        "window.popup, window.popup decoration, "
        "window.background.popup, window.background.popup decoration {"
        "  margin: 0;"
        "  padding: 0;"
        "  border: none;"
        "  border-radius: 0;"
        "  box-shadow: none;"
        "  background-color: transparent;"
        "  background-image: none;"
        "}"
        "menu#desktop-context-menu, menu.desktop-context-menu {"
        "  background-color: rgba(12, 12, 12, 0.16);"
        "  background-image: none;"
        "  border: none;"
        "  border-radius: 12px;"
        "  box-shadow: none;"
        "}"
        "menu#desktop-context-menu box, menu.desktop-context-menu box {"
        "  margin: 0;"
        "  padding: 0;"
        "}"
        "menu#desktop-context-menu menuitem, menu.desktop-context-menu menuitem {"
        "  margin: 0;"
        "  padding: 0;"
        "  min-height: 0;"
        "  min-width: 0;"
        "  background-color: transparent;"
        "  background-image: none;"
        "  border-radius: 8px;"
        "}"
        "menu#desktop-context-menu menuitem > label, menu.desktop-context-menu menuitem > label {"
        "  margin: 0;"
        "  padding: 6px 10px;"
        "}"
        "menu#desktop-context-menu menuitem:hover, menu.desktop-context-menu menuitem:hover {"
        "  background-color: rgba(255, 255, 255, 0.10);"
        "}"
        "menu#desktop-context-menu separator, menu.desktop-context-menu separator {"
        "  margin: 2px 0;"
        "  padding: 0;"
        "  min-height: 1px;"
        "  background-color: rgba(255, 255, 255, 0.10);"
        "}"
        "menu#desktop-context-menu label, menu.desktop-context-menu label {"
        "  color: rgba(255, 255, 255, 0.96);"
        "  text-shadow: none;"
        "}"
        "window#desktop-blur-dialog, window#desktop-blur-dialog decoration, "
        "window.desktop-blur-dialog, window.desktop-blur-dialog decoration {"
        "  margin: 0;"
        "  padding: 0;"
        "  border: none;"
        "  box-shadow: none;"
        "  background-color: transparent;"
        "  background-image: none;"
        "}"
        "#desktop-blur-dialog-content {"
        "  background-color: rgba(12, 12, 12, 0.16);"
        "  background-image: none;"
        "  border-radius: 12px;"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "}"
        "window#desktop-blur-dialog label, window.desktop-blur-dialog label {"
        "  color: rgba(255, 255, 255, 0.96);"
        "  text-shadow: none;"
        "}"
        "window#desktop-blur-dialog entry, window.desktop-blur-dialog entry {"
        "  background-color: rgba(255, 255, 255, 0.08);"
        "  background-image: none;"
        "  color: rgba(255, 255, 255, 0.96);"
        "  border: 1px solid rgba(255, 255, 255, 0.10);"
        "  box-shadow: none;"
        "}"
        "window#desktop-blur-dialog button, window.desktop-blur-dialog button {"
        "  background-color: rgba(255, 255, 255, 0.08);"
        "  background-image: none;"
        "  color: rgba(255, 255, 255, 0.96);"
        "  border: 1px solid rgba(255, 255, 255, 0.10);"
        "  box-shadow: none;"
        "  border-radius: 8px;"
        "}"
        "window#desktop-blur-dialog button:hover, window.desktop-blur-dialog button:hover {"
        "  background-color: rgba(255, 255, 255, 0.14);"
        "  background-image: none;"
        "}"
        "window#desktop-blur-dialog button:active, window.desktop-blur-dialog button:active {"
        "  background-color: rgba(255, 255, 255, 0.18);"
        "  background-image: none;"
        "}"
        "window#desktop-blur-dialog button label, window.desktop-blur-dialog button label {"
        "  color: rgba(255, 255, 255, 0.96);"
        "  text-shadow: none;"
        "}",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css), 800);
}

static void build_desktop_ui(void) {
    if (main_window || icon_layout) return;

    init_main_window();
    setup_drag_dest();
    setup_layout_events();
    apply_desktop_css();

    load_all_widgets(icon_layout);
    refresh_icons();

    g_signal_connect(main_window, "destroy", G_CALLBACK(on_main_window_destroy), NULL);
    gtk_widget_show_all(main_window);
    load_saved_wallpaper();
    malloc_trim(0);
}

static gboolean try_recover_desktop(gpointer user_data) {
    (void)user_data;

    if (!desktop_has_available_monitor()) return G_SOURCE_CONTINUE;

    recovery_source_id = 0;
    if (restart_desktop_process()) {
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_REMOVE;
}

static void on_main_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;

    deselect_all();
    is_selecting = FALSE;
    main_window = NULL;
    icon_layout = NULL;

    if (recovery_source_id == 0) {
        recovery_source_id = g_timeout_add(1000, try_recover_desktop, NULL);
    }
}

static gboolean restart_desktop_process(void) {
    GError *error = NULL;
    gchar *argv[] = { desktop_executable_path, NULL };

    if (!desktop_executable_path || desktop_executable_path[0] == '\0') {
        g_warning("[Desktop] Cannot restart: executable path is unavailable");
        return FALSE;
    }

    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        g_warning("[Desktop] Failed to restart desktop: %s", error ? error->message : "unknown error");
        if (error) g_error_free(error);
        return FALSE;
    }

    return TRUE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    desktop_executable_path = g_file_read_link("/proc/self/exe", NULL);
    if (!desktop_executable_path && argc > 0 && argv[0] && argv[0][0] != '\0') {
        desktop_executable_path = g_strdup(argv[0]);
    }

    build_desktop_ui();
    gtk_main();
    g_free(desktop_executable_path);
    return 0;
}
