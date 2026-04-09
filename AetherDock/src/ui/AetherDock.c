#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <gtk-layer-shell.h>
#include <gio/gdesktopappinfo.h>
#include <wayland-client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "launcher.h"
#include "pager.h"
#include "trash.h"
#include "drives.h"
#include "utils.h"
#include "logic/app_manager.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

static gboolean is_wayland_session = FALSE;
static struct wl_display *wl_display_conn = NULL;
static struct wl_seat *wl_seat_obj = NULL;
static struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager = NULL;
static GHashTable *wayland_toplevels = NULL;
static guint wayland_event_source_id = 0;
static guint recovery_source_id = 0;
static char *dock_executable_path = NULL;
extern GtkWidget *main_window;
extern GtkWidget *box;
extern GList *pinned_apps;
void update_window_list(void);

typedef struct {
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    gchar *app_id;
    gchar *title;
    guint32 state_flags;
} WaylandToplevel;

enum {
    WAYLAND_TOPLEVEL_STATE_MAXIMIZED = 1u << 0,
    WAYLAND_TOPLEVEL_STATE_MINIMIZED = 1u << 1,
    WAYLAND_TOPLEVEL_STATE_ACTIVATED = 1u << 2,
};

static gboolean flush_wayland_events(gpointer data);
static void free_wayland_toplevel(gpointer data);
static void wayland_schedule_refresh(void);
static gboolean dock_has_available_monitor(void);
static gboolean get_primary_monitor_geometry(GdkDisplay *display, GdkRectangle *geom);
static gboolean restart_dock_process(void);
static gboolean try_recover_dock(gpointer data);
static void on_dock_window_destroy(GtkWidget *widget, gpointer user_data);
static gchar *normalize_app_id(const gchar *app_id);
static gchar *normalize_match_key(const gchar *value);
static gboolean app_id_equals(const gchar *lhs, const gchar *rhs);
static gboolean pinned_app_contains(const gchar *app_id);
static gint score_app_id_candidate(const gchar *app_id, const gchar *candidate);
static gchar *resolve_desktop_file_path_for_app_id(const gchar *app_id);
static gchar *desktop_file_path_from_app_id(const gchar *app_id);
static GdkPixbuf *icon_from_app_id(const gchar *app_id);
static WaylandToplevel *wayland_toplevel_from_handle(struct zwlr_foreign_toplevel_handle_v1 *handle);
static void handle_toplevel_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *title);
static void handle_toplevel_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *app_id);
static void handle_toplevel_output_enter(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_output *output);
static void handle_toplevel_output_leave(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_output *output);
static void handle_toplevel_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_array *state);
static void handle_toplevel_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle);
static void handle_toplevel_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle);
static void handle_toplevel_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct zwlr_foreign_toplevel_handle_v1 *parent);
static void handle_manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager, struct zwlr_foreign_toplevel_handle_v1 *toplevel);
static void handle_manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager);

static const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_listener = {
    .title = handle_toplevel_title,
    .app_id = handle_toplevel_app_id,
    .output_enter = handle_toplevel_output_enter,
    .output_leave = handle_toplevel_output_leave,
    .state = handle_toplevel_state,
    .done = handle_toplevel_done,
    .closed = handle_toplevel_closed,
    .parent = handle_toplevel_parent,
};

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    .toplevel = handle_manager_toplevel,
    .finished = handle_manager_finished,
};

static gboolean dock_has_available_monitor(void) {
    GdkDisplay *display = gdk_display_get_default();

    if (!display) {
        return FALSE;
    }

    if (gdk_display_get_primary_monitor(display)) {
        return TRUE;
    }

    return gdk_display_get_n_monitors(display) > 0;
}

static gboolean get_primary_monitor_geometry(GdkDisplay *display, GdkRectangle *geom) {
    GdkMonitor *monitor = NULL;

    if (!display || !geom) {
        return FALSE;
    }

    monitor = gdk_display_get_primary_monitor(display);
    if (!monitor && gdk_display_get_n_monitors(display) > 0) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    if (!monitor) {
        return FALSE;
    }

    gdk_monitor_get_geometry(monitor, geom);
    return TRUE;
}

static gboolean restart_dock_process(void) {
    GError *error = NULL;
    gchar *argv[] = { dock_executable_path, NULL };

    if (!dock_executable_path || dock_executable_path[0] == '\0') {
        g_warning("[AetherDock] Cannot restart: executable path is unavailable");
        return FALSE;
    }

    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        g_warning("[AetherDock] Failed to restart dock: %s",
                  error ? error->message : "unknown error");
        if (error) {
            g_error_free(error);
        }
        return FALSE;
    }

    return TRUE;
}

static gboolean try_recover_dock(gpointer data) {
    (void)data;

    if (!dock_has_available_monitor()) {
        return G_SOURCE_CONTINUE;
    }

    if (restart_dock_process()) {
        recovery_source_id = 0;
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void on_dock_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;

    if (recovery_source_id == 0) {
        recovery_source_id = g_timeout_add(1000, try_recover_dock, NULL);
    }
}

static void registry_add_object(void *data, struct wl_registry *registry,
                                uint32_t name, const char *interface, uint32_t version) {
    (void)data;

    if (strcmp(interface, wl_seat_interface.name) == 0) {
        wl_seat_obj = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        return;
    }

    if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        uint32_t bind_version = version < 1 ? version : 1;
        toplevel_manager = wl_registry_bind(registry, name,
                                            &zwlr_foreign_toplevel_manager_v1_interface,
                                            bind_version);
    }
}

static void registry_remove_object(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_add_object,
    .global_remove = registry_remove_object,
};

static void init_wayland_protocols(void) {
    struct wl_registry *registry;

    if (!is_wayland_session) {
        return;
    }

    wl_display_conn = gdk_wayland_display_get_wl_display(gdk_display_get_default());
    if (!wl_display_conn) {
        g_warning("Wayland session detected, but wl_display is unavailable.");
        return;
    }

    registry = wl_display_get_registry(wl_display_conn);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(wl_display_conn);
    wl_registry_destroy(registry);

    if (!toplevel_manager) {
        g_warning("Compositor does not expose wlr-foreign-toplevel-management. "
                  "Wayland task/window integration is not ready yet.");
        return;
    }

    wayland_toplevels = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free_wayland_toplevel);
    zwlr_foreign_toplevel_manager_v1_add_listener(toplevel_manager, &manager_listener, NULL);
    wl_display_roundtrip(wl_display_conn);
    wayland_event_source_id = g_timeout_add(80, flush_wayland_events, NULL);
}

static void setup_dock_window_layer(GtkWidget *window) {
    if (is_wayland_session) {
        gtk_layer_init_for_window(GTK_WINDOW(window));
        gtk_layer_set_namespace(GTK_WINDOW(window), "AetherDock");
        gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, 2);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(window));
    }
}

static gboolean flush_wayland_events(gpointer data) {
    (void)data;

    if (!wl_display_conn) {
        return G_SOURCE_CONTINUE;
    }

    wl_display_dispatch_pending(wl_display_conn);
    wl_display_flush(wl_display_conn);
    return G_SOURCE_CONTINUE;
}

static void free_wayland_toplevel(gpointer data) {
    WaylandToplevel *item = (WaylandToplevel *)data;

    if (!item) {
        return;
    }

    g_free(item->app_id);
    g_free(item->title);
    g_free(item);
}

static void wayland_schedule_refresh(void) {
    if (is_wayland_session && box != NULL) {
        update_window_list();
    }
}

static gchar *normalize_app_id(const gchar *app_id) {
    gchar *trimmed;
    gchar *normalized;
    size_t len;

    if (!app_id || !*app_id) {
        return g_strdup("unknown");
    }

    trimmed = g_strdup(app_id);
    g_strstrip(trimmed);
    len = strlen(trimmed);
    if (len > 8 && g_str_has_suffix(trimmed, ".desktop")) {
        trimmed[len - 8] = '\0';
    }

    normalized = g_ascii_strdown(trimmed, -1);
    g_free(trimmed);
    return normalized;
}

static gchar *normalize_match_key(const gchar *value) {
    gchar *normalized;
    gchar *base;
    gchar *compact;
    gsize len = 0;

    if (!value || !*value) {
        return g_strdup("");
    }

    normalized = normalize_app_id(value);
    base = g_path_get_basename(normalized);

    compact = g_malloc0(strlen(base) + 1);
    for (const gchar *p = base; *p != '\0'; p++) {
        if (g_ascii_isalnum(*p)) {
            compact[len++] = g_ascii_tolower(*p);
        }
    }

    g_free(base);
    g_free(normalized);
    return compact;
}

static gboolean app_id_equals(const gchar *lhs, const gchar *rhs) {
    gchar *left_norm;
    gchar *right_norm;
    gboolean equal;

    if (lhs == NULL || rhs == NULL) {
        return FALSE;
    }

    left_norm = normalize_app_id(lhs);
    right_norm = normalize_app_id(rhs);
    equal = g_strcmp0(left_norm, right_norm) == 0;
    g_free(left_norm);
    g_free(right_norm);
    return equal;
}

static gint score_app_id_candidate(const gchar *app_id, const gchar *candidate) {
    gchar *normalized_app_id;
    gchar *normalized_candidate;
    gchar *compact_app_id;
    gchar *compact_candidate;
    gint score = 0;

    if (!app_id || !*app_id || !candidate || !*candidate) {
        return 0;
    }

    normalized_app_id = normalize_app_id(app_id);
    normalized_candidate = normalize_app_id(candidate);
    compact_app_id = normalize_match_key(app_id);
    compact_candidate = normalize_match_key(candidate);

    if (g_strcmp0(normalized_app_id, normalized_candidate) == 0) {
        score = 100;
    } else if (*compact_app_id != '\0' && g_strcmp0(compact_app_id, compact_candidate) == 0) {
        score = 90;
    } else if (*normalized_app_id != '\0' &&
               (g_str_has_suffix(normalized_app_id, normalized_candidate) ||
                g_str_has_suffix(normalized_candidate, normalized_app_id))) {
        score = 75;
    } else if (*compact_app_id != '\0' &&
               (g_str_has_suffix(compact_app_id, compact_candidate) ||
                g_str_has_suffix(compact_candidate, compact_app_id))) {
        score = 70;
    } else if (*compact_app_id != '\0' &&
               (strstr(compact_app_id, compact_candidate) != NULL ||
                strstr(compact_candidate, compact_app_id) != NULL)) {
        score = 50;
    }

    g_free(normalized_app_id);
    g_free(normalized_candidate);
    g_free(compact_app_id);
    g_free(compact_candidate);
    return score;
}

static gboolean pinned_app_contains(const gchar *app_id) {
    for (GList *l = pinned_apps; l != NULL; l = l->next) {
        if (app_id_equals((const gchar *)l->data, app_id)) {
            return TRUE;
        }
    }

    return FALSE;
}

static gchar *resolve_desktop_file_path_for_app_id(const gchar *app_id) {
    static GHashTable *resolved_paths = NULL;
    gchar *normalized_app_id;
    const gchar *cached_value;
    GDesktopAppInfo *app_info = NULL;
    gchar *desktop_id;
    gchar *resolved_path = NULL;

    if (!app_id || !*app_id) {
        return NULL;
    }

    if (resolved_paths == NULL) {
        resolved_paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    }

    normalized_app_id = normalize_app_id(app_id);
    cached_value = g_hash_table_lookup(resolved_paths, normalized_app_id);
    if (cached_value != NULL) {
        gchar *result = (*cached_value != '\0') ? g_strdup(cached_value) : NULL;
        g_free(normalized_app_id);
        return result;
    }

    desktop_id = g_strdup_printf("%s.desktop", normalized_app_id);
    app_info = g_desktop_app_info_new(desktop_id);
    g_free(desktop_id);

    if (app_info == NULL) {
        desktop_id = g_strdup_printf("%s.desktop", app_id);
        app_info = g_desktop_app_info_new(desktop_id);
        g_free(desktop_id);
    }

    if (app_info != NULL) {
        resolved_path = g_strdup(g_desktop_app_info_get_filename(app_info));
        g_object_unref(app_info);
    } else {
        GList *all_apps = g_app_info_get_all();
        gint best_score = 0;

        for (GList *l = all_apps; l != NULL; l = l->next) {
            GAppInfo *candidate = G_APP_INFO(l->data);
            gint score = 0;

            if (!G_IS_DESKTOP_APP_INFO(candidate) || !g_app_info_should_show(candidate)) {
                continue;
            }

            const gchar *filename = g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(candidate));
            const gchar *startup_wm_class = g_desktop_app_info_get_startup_wm_class(G_DESKTOP_APP_INFO(candidate));
            const gchar *executable = g_app_info_get_executable(candidate);

            score = MAX(score, score_app_id_candidate(normalized_app_id, filename));
            score = MAX(score, score_app_id_candidate(normalized_app_id, startup_wm_class));
            score = MAX(score, score_app_id_candidate(normalized_app_id, executable));
            score = MAX(score, score_app_id_candidate(normalized_app_id, g_app_info_get_id(candidate)));
            score = MAX(score, score_app_id_candidate(normalized_app_id, g_app_info_get_name(candidate)));

            if (score > best_score) {
                g_free(resolved_path);
                resolved_path = g_strdup(filename);
                best_score = score;
                if (best_score >= 100) {
                    break;
                }
            }
        }

        g_list_free_full(all_apps, g_object_unref);
    }

    g_hash_table_insert(resolved_paths,
                        g_strdup(normalized_app_id),
                        g_strdup(resolved_path != NULL ? resolved_path : ""));
    g_free(normalized_app_id);
    return resolved_path;
}

static gchar *desktop_file_path_from_app_id(const gchar *app_id) {
    return resolve_desktop_file_path_for_app_id(app_id);
}

static GdkPixbuf *icon_from_app_id(const gchar *app_id) {
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    GDesktopAppInfo *app_info = NULL;
    GdkPixbuf *pixbuf = NULL;
    gchar *normalized_app_id;
    gchar *desktop_file_path;

    if (!app_id || !*app_id) {
        return gtk_icon_theme_load_icon(icon_theme, "application-x-executable", 48,
                                        GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    }

    normalized_app_id = normalize_app_id(app_id);
    desktop_file_path = resolve_desktop_file_path_for_app_id(app_id);

    if (desktop_file_path != NULL) {
        app_info = g_desktop_app_info_new_from_filename(desktop_file_path);
    }

    if (app_info != NULL) {
        gchar *icon_name = g_desktop_app_info_get_string(app_info, "Icon");
        if (icon_name != NULL) {
            if (g_path_is_absolute(icon_name)) {
                pixbuf = gdk_pixbuf_new_from_file_at_scale(icon_name, 48, 48, TRUE, NULL);
            } else {
                pixbuf = gtk_icon_theme_load_icon(icon_theme, icon_name, 48,
                                                  GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
            }
            g_free(icon_name);
        }
        g_object_unref(app_info);
    }

    if (pixbuf == NULL) {
        pixbuf = gtk_icon_theme_load_icon(icon_theme, normalized_app_id, 48,
                                          GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    }

    if (pixbuf == NULL) {
        pixbuf = gtk_icon_theme_load_icon(icon_theme, app_id, 48,
                                          GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    }

    if (pixbuf == NULL && desktop_file_path != NULL) {
        gchar *desktop_basename = g_path_get_basename(desktop_file_path);
        pixbuf = gtk_icon_theme_load_icon(icon_theme, desktop_basename, 48,
                                          GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
        g_free(desktop_basename);
    }

    if (pixbuf == NULL) {
        pixbuf = gtk_icon_theme_load_icon(icon_theme, "application-x-executable", 48,
                                          GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    }

    g_free(desktop_file_path);
    g_free(normalized_app_id);

    return pixbuf;
}

static WaylandToplevel *wayland_toplevel_from_handle(struct zwlr_foreign_toplevel_handle_v1 *handle) {
    if (!wayland_toplevels) {
        return NULL;
    }

    return g_hash_table_lookup(wayland_toplevels, handle);
}

static void handle_toplevel_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *title) {
    (void)data;
    WaylandToplevel *item = wayland_toplevel_from_handle(handle);

    if (!item) {
        return;
    }

    g_free(item->title);
    item->title = g_strdup(title);
    wayland_schedule_refresh();
}

static void handle_toplevel_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *app_id) {
    (void)data;
    WaylandToplevel *item = wayland_toplevel_from_handle(handle);
    gchar *normalized_app_id;

    if (!item) {
        return;
    }

    normalized_app_id = normalize_app_id((app_id && *app_id) ? app_id : "unknown");
    g_free(item->app_id);
    item->app_id = normalized_app_id;
    wayland_schedule_refresh();
}

static void handle_toplevel_output_enter(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_output *output) {
    (void)data;
    (void)handle;
    (void)output;
}

static void handle_toplevel_output_leave(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_output *output) {
    (void)data;
    (void)handle;
    (void)output;
}

static void handle_toplevel_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_array *state) {
    (void)data;
    WaylandToplevel *item = wayland_toplevel_from_handle(handle);
    uint32_t *entry;
    size_t count;

    if (!item) {
        return;
    }

    item->state_flags = 0;
    entry = state->data;
    count = state->size / sizeof(uint32_t);

    for (size_t i = 0; i < count; i++) {
        if (entry[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
            item->state_flags |= WAYLAND_TOPLEVEL_STATE_ACTIVATED;
        } else if (entry[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) {
            item->state_flags |= WAYLAND_TOPLEVEL_STATE_MINIMIZED;
        } else if (entry[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED) {
            item->state_flags |= WAYLAND_TOPLEVEL_STATE_MAXIMIZED;
        }
    }

    wayland_schedule_refresh();
}

static void handle_toplevel_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)data;
    (void)handle;
    wayland_schedule_refresh();
}

static void handle_toplevel_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)data;

    if (!wayland_toplevels) {
        return;
    }

    g_hash_table_remove(wayland_toplevels, handle);
    zwlr_foreign_toplevel_handle_v1_destroy(handle);
    wayland_schedule_refresh();
}

static void handle_toplevel_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct zwlr_foreign_toplevel_handle_v1 *parent) {
    (void)data;
    (void)handle;
    (void)parent;
}

static void handle_manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager, struct zwlr_foreign_toplevel_handle_v1 *toplevel) {
    WaylandToplevel *item;

    (void)data;
    (void)manager;

    item = g_new0(WaylandToplevel, 1);
    item->handle = toplevel;
    item->app_id = normalize_app_id("unknown");

    g_hash_table_insert(wayland_toplevels, toplevel, item);
    zwlr_foreign_toplevel_handle_v1_add_listener(toplevel, &toplevel_listener, NULL);
    wayland_schedule_refresh();
}

static void handle_manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager) {
    (void)data;
    (void)manager;
}


/* Window Group structure for grouping windows by WM_CLASS */
typedef struct {
    char *wm_class;
    GList *windows;  /* List of WaylandToplevel* on Wayland */
    GdkPixbuf *icon;
    GtkWidget *button;
    int active_index;
    char *desktop_file_path;  /* Path to .desktop file */
    gboolean is_pinned;       /* Whether app is pinned */
} WindowGroup;

GtkWidget *main_window;
GtkWidget *box;
GtkWidget *system_separator;
GtkWidget *context_menu_window = NULL;
GdkSeat *context_menu_grab_seat = NULL;
GHashTable *window_groups; /* wm_class -> WindowGroup */
GList *pinned_apps = NULL; /* List of pinned wm_class strings */
GHashTable *launching_apps = NULL; /* desktop file path -> active launch marker */

typedef struct {
    gchar *desktop_file_path;
} LaunchTimeoutData;

typedef struct {
    WindowGroup *group;
    void (*action)(GtkWidget *, gpointer);
} ContextMenuActionData;

static void free_context_menu_action_data(gpointer data, GClosure *closure) {
    (void)closure;
    g_free(data);
}


/* Dock Realize Callback - Set WM_STRUT for space reservation */
static void on_dock_realize(GtkWidget *widget, gpointer data) {
    (void)data;
    GdkWindow *gdk_window = gtk_widget_get_window(widget);
    if (!gdk_window) {
        return;
    }

    if (is_wayland_session) {
        gdk_window_set_type_hint(gdk_window, GDK_WINDOW_TYPE_HINT_DOCK);
        return;
    }

    GdkDisplay *display = gdk_window_get_display(gdk_window);
    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);
    
    /* Set window as dock type */
    gdk_window_set_type_hint(gdk_window, GDK_WINDOW_TYPE_HINT_DOCK);
    
    /* Reserve 60px at bottom */
    gulong strut[12] = {0};
    strut[3] = 55;  /* bottom */
    strut[10] = 0;  /* bottom_start_x */
    strut[11] = geometry.width;  /* bottom_end_x */
    
    gdk_property_change(gdk_window, 
                       gdk_atom_intern("_NET_WM_STRUT_PARTIAL", FALSE),
                       gdk_atom_intern("CARDINAL", FALSE),
                       32, GDK_PROP_MODE_REPLACE,
                       (guchar *)strut, 12);
    
    /* Also set _NET_WM_STRUT for older window managers */
    gulong simple_strut[4] = {0, 0, 0, 55};  /* left, right, top, bottom */
    gdk_property_change(gdk_window,
                       gdk_atom_intern("_NET_WM_STRUT", FALSE),
                       gdk_atom_intern("CARDINAL", FALSE),
                       32, GDK_PROP_MODE_REPLACE,
                       (guchar *)simple_strut, 4);
}


/* Function prototypes */
static void on_dock_realize(GtkWidget *widget, gpointer data);
static void on_window_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data);
static void close_context_menu(void);
static gboolean on_context_menu_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data);
static gboolean on_context_menu_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data);
static void on_context_menu_window_active_notify(GObject *object, GParamSpec *pspec, gpointer data);
static gboolean on_context_menu_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
static gboolean on_context_menu_grab_broken(GtkWidget *widget, GdkEventGrabBroken *event, gpointer data);
static void on_context_menu_item_clicked(GtkWidget *widget, gpointer data);
static GtkWidget *create_context_menu_item(const gchar *label, WindowGroup *group, void (*action)(GtkWidget *, gpointer));
static GtkWidget *create_context_menu_separator(void);
static GtkWidget *create_wayland_menu_item(const gchar *label, WindowGroup *group, void (*action)(GtkWidget *, gpointer));
static void position_context_menu(GtkWidget *menu_window, GdkEventButton *event);
static GtkWidget *create_launch_ring(void);
static gboolean on_launch_ring_draw(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean rotate_launch_ring(gpointer data);
static void on_launch_ring_destroy(GtkWidget *widget, gpointer data);
static void attach_launch_ring(GtkWidget *button);
static void mark_app_launching(const gchar *desktop_file_path);
static gboolean clear_stale_launch_state(gpointer data);
static gboolean is_app_launching(const gchar *desktop_file_path);
static void unmark_app_launching(const gchar *desktop_file_path);

void update_window_list();
void on_button_clicked(GtkWidget *widget, gpointer data);
gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
void create_context_menu(WindowGroup *group, GdkEventButton *event);
void on_pin_clicked(GtkWidget *menuitem, gpointer data);
void on_new_window_clicked(GtkWidget *menuitem, gpointer data);
void on_close_all_clicked(GtkWidget *menuitem, gpointer data);
void on_run_with_gpu_clicked(GtkWidget *menuitem, gpointer data);
void load_pinned_apps();
void save_pinned_apps();

/* Window size allocate callback for centering */
static gboolean on_window_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    /* Create a rounded rectangle shape for the window itself */
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    double radius = 14.0; /* Match CSS border-radius */

    /* Clear the background completely */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);

    /* Draw the rounded mask */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_new_sub_path(cr);
    cairo_arc(cr, width - radius, radius, radius, -G_PI/2, 0);
    cairo_arc(cr, width - radius, height - radius, radius, 0, G_PI/2);
    cairo_arc(cr, radius, height - radius, radius, G_PI/2, G_PI);
    cairo_arc(cr, radius, radius, radius, G_PI, 3*G_PI/2);
    cairo_close_path(cr);
    
    /* Paint it with a solid, but since it's RGBA visual it will allow the CSS background through */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0); 
    cairo_fill(cr);

    return FALSE; /* Let GTK draw the CSS over our cairo mask */
}

static void close_context_menu(void) {
    if (context_menu_grab_seat) {
        gdk_seat_ungrab(context_menu_grab_seat);
        context_menu_grab_seat = NULL;
    }

    if (context_menu_window) {
        gtk_widget_destroy(context_menu_window);
        context_menu_window = NULL;
    }
}

static gboolean on_context_menu_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    (void)widget;
    (void)event;
    (void)data;
    close_context_menu();
    return FALSE;
}

static gboolean on_context_menu_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget;
    (void)data;
    if (event->keyval == GDK_KEY_Escape) {
        close_context_menu();
        return TRUE;
    }
    return FALSE;
}

static void on_context_menu_window_active_notify(GObject *object, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    (void)data;

    if (!gtk_window_is_active(GTK_WINDOW(object))) {
        close_context_menu();
    }
}

static gboolean on_context_menu_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    GtkAllocation allocation;

    (void)data;
    gtk_widget_get_allocation(widget, &allocation);

    if (event->x < 0 || event->y < 0 ||
        event->x >= allocation.width || event->y >= allocation.height) {
        close_context_menu();
        return TRUE;
    }

    return FALSE;
}

static gboolean on_context_menu_grab_broken(GtkWidget *widget, GdkEventGrabBroken *event, gpointer data) {
    (void)widget;
    (void)event;
    (void)data;
    context_menu_grab_seat = NULL;
    close_context_menu();
    return FALSE;
}

static void on_context_menu_item_clicked(GtkWidget *widget, gpointer data) {
    ContextMenuActionData *action_data = (ContextMenuActionData *)data;
    close_context_menu();
    action_data->action(widget, action_data->group);
}

static GtkWidget *create_context_menu_item(const gchar *label, WindowGroup *group, void (*action)(GtkWidget *, gpointer)) {
    GtkWidget *button = gtk_button_new_with_label(label);
    ContextMenuActionData *action_data = g_new0(ContextMenuActionData, 1);

    gtk_widget_set_name(button, "context-menu-item");
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);

    action_data->group = group;
    action_data->action = action;
    g_signal_connect_data(button,
                          "clicked",
                          G_CALLBACK(on_context_menu_item_clicked),
                          action_data,
                          free_context_menu_action_data,
                          0);

    return button;
}

static GtkWidget *create_context_menu_separator(void) {
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(separator, "context-menu-separator");
    return separator;
}

static GtkWidget *create_wayland_menu_item(const gchar *label, WindowGroup *group, void (*action)(GtkWidget *, gpointer)) {
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    ContextMenuActionData *action_data = g_new0(ContextMenuActionData, 1);
    GtkStyleContext *context = gtk_widget_get_style_context(item);

    gtk_widget_set_name(item, "dock-context-menu-item");
    gtk_style_context_add_class(context, "dock-context-menu-item");
    action_data->group = group;
    action_data->action = action;
    g_signal_connect_data(item,
                          "activate",
                          G_CALLBACK(on_context_menu_item_clicked),
                          action_data,
                          free_context_menu_action_data,
                          0);
    return item;
}

static void position_context_menu(GtkWidget *menu_window, GdkEventButton *event) {
    GtkRequisition min_req = {0};
    GtkRequisition nat_req = {0};
    GdkDisplay *display;
    GdkMonitor *monitor;
    GdkRectangle geometry = {0};
    gint menu_width;
    gint menu_height;
    gint x;
    gint y;
    const gint gap = 8;
    const gint margin = 8;

    gtk_widget_get_preferred_size(menu_window, &min_req, &nat_req);
    menu_width = nat_req.width > 0 ? nat_req.width : min_req.width;
    menu_height = nat_req.height > 0 ? nat_req.height : min_req.height;

    display = gtk_widget_get_display(menu_window);
    monitor = gdk_display_get_monitor_at_point(display, (gint)event->x_root, (gint)event->y_root);
    if (!monitor) {
        monitor = gdk_display_get_primary_monitor(display);
    }
    if (!monitor) {
        gtk_window_move(GTK_WINDOW(menu_window), (gint)event->x_root, (gint)event->y_root);
        return;
    }

    gdk_monitor_get_geometry(monitor, &geometry);

    x = (gint)event->x_root - (menu_width / 2);
    y = (gint)event->y_root - menu_height - gap;

    if (y < geometry.y + margin) {
        y = (gint)event->y_root + gap;
    }

    if (x + menu_width > geometry.x + geometry.width - margin) {
        x = geometry.x + geometry.width - menu_width - margin;
    }
    if (x < geometry.x + margin) {
        x = geometry.x + margin;
    }

    if (y + menu_height > geometry.y + geometry.height - margin) {
        y = geometry.y + geometry.height - menu_height - margin;
    }
    if (y < geometry.y + margin) {
        y = geometry.y + margin;
    }

    gtk_window_move(GTK_WINDOW(menu_window), x, y);
}

static GtkWidget *create_launch_ring(void) {
    GtkWidget *ring = gtk_drawing_area_new();
    gdouble *angle = g_new0(gdouble, 1);

    gtk_widget_set_name(ring, "launch-ring");
    gtk_widget_set_size_request(ring, 40, 40);
    gtk_widget_set_halign(ring, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(ring, GTK_ALIGN_CENTER);
    gtk_widget_set_can_focus(ring, FALSE);

    g_object_set_data_full(G_OBJECT(ring), "ring-angle", angle, g_free);
    g_signal_connect(ring, "draw", G_CALLBACK(on_launch_ring_draw), NULL);
    g_signal_connect(ring, "destroy", G_CALLBACK(on_launch_ring_destroy), NULL);

    guint timeout_id = g_timeout_add(16, rotate_launch_ring, ring);
    g_object_set_data(G_OBJECT(ring), "ring-timeout-id", GUINT_TO_POINTER(timeout_id));

    return ring;
}

static gboolean on_launch_ring_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    gdouble *angle = g_object_get_data(G_OBJECT(widget), "ring-angle");
    if (!angle) return FALSE;

    gint width = gtk_widget_get_allocated_width(widget);
    gint height = gtk_widget_get_allocated_height(widget);
    gdouble radius = MIN(width, height) / 2.0 - 3.0;
    gdouble cx = width / 2.0;
    gdouble cy = height / 2.0;
    gdouble start = *angle;
    gdouble end = start + (G_PI * 1.15);

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
    cairo_set_source_rgba(cr, 0.0, 0.99, 0.82, 0.13);
    cairo_set_line_width(cr, 1.4);
    cairo_stroke(cr);

    cairo_arc(cr, cx, cy, radius, start, end);
    cairo_set_source_rgba(cr, 0.0, 0.99, 0.82, 0.95);
    cairo_set_line_width(cr, 2.2);
    cairo_stroke(cr);

    cairo_arc(cr, cx, cy, radius, start, end);
    cairo_set_source_rgba(cr, 0.0, 0.99, 0.82, 0.28);
    cairo_set_line_width(cr, 5.4);
    cairo_stroke(cr);

    return FALSE;
}

static gboolean rotate_launch_ring(gpointer data) {
    GtkWidget *ring = GTK_WIDGET(data);
    if (!GTK_IS_WIDGET(ring) || !gtk_widget_get_visible(ring)) {
        return G_SOURCE_REMOVE;
    }

    gdouble *angle = g_object_get_data(G_OBJECT(ring), "ring-angle");
    if (!angle) return G_SOURCE_REMOVE;

    *angle += 0.22;
    if (*angle > 2 * G_PI) {
        *angle -= 2 * G_PI;
    }

    gtk_widget_queue_draw(ring);
    return G_SOURCE_CONTINUE;
}

static void on_launch_ring_destroy(GtkWidget *widget, gpointer data) {
    (void)data;
    guint timeout_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "ring-timeout-id"));
    if (timeout_id != 0) {
        g_source_remove(timeout_id);
        g_object_set_data(G_OBJECT(widget), "ring-timeout-id", NULL);
    }
}

static void attach_launch_ring(GtkWidget *button) {
    GtkWidget *overlay = gtk_bin_get_child(GTK_BIN(button));
    GtkWidget *ring = g_object_get_data(G_OBJECT(button), "launch-ring");

    if (!GTK_IS_OVERLAY(overlay) || ring != NULL) {
        return;
    }

    ring = create_launch_ring();
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ring);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), ring, TRUE);
    g_object_set_data(G_OBJECT(button), "launch-ring", ring);
    gtk_widget_show(ring);
}

static void mark_app_launching(const gchar *desktop_file_path) {
    LaunchTimeoutData *timeout_data;

    if (!desktop_file_path || !launching_apps) return;
    if (g_hash_table_contains(launching_apps, desktop_file_path)) return;

    g_hash_table_insert(launching_apps, g_strdup(desktop_file_path), GINT_TO_POINTER(1));

    timeout_data = g_new0(LaunchTimeoutData, 1);
    timeout_data->desktop_file_path = g_strdup(desktop_file_path);
    g_timeout_add_seconds(12, clear_stale_launch_state, timeout_data);
}

static gboolean clear_stale_launch_state(gpointer data) {
    LaunchTimeoutData *timeout_data = (LaunchTimeoutData *)data;

    if (launching_apps && g_hash_table_remove(launching_apps, timeout_data->desktop_file_path)) {
        update_window_list();
    }

    g_free(timeout_data->desktop_file_path);
    g_free(timeout_data);
    return G_SOURCE_REMOVE;
}

static gboolean is_app_launching(const gchar *desktop_file_path) {
    return desktop_file_path && launching_apps &&
           g_hash_table_contains(launching_apps, desktop_file_path);
}

static void unmark_app_launching(const gchar *desktop_file_path) {
    if (!desktop_file_path || !launching_apps) return;
    g_hash_table_remove(launching_apps, desktop_file_path);
}

/* Window size allocate callback for centering and shaping */
static void on_window_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
    (void)data;
    GdkWindow *gdk_window = gtk_widget_get_window(widget);
    if (!gdk_window) return;
    
    /* Create a shape mask for true X11 rounding (blur respect) */
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_A1, allocation->width, allocation->height);
    cairo_t *cr = cairo_create(surface);
    
    double radius = 14.0; /* Match CSS border-radius */
    
    /* Draw rounded rectangle mask */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, allocation->width - radius, radius, radius, -G_PI/2, 0);
    cairo_arc(cr, allocation->width - radius, allocation->height - radius, radius, 0, G_PI/2);
    cairo_arc(cr, radius, allocation->height - radius, radius, G_PI/2, G_PI);
    cairo_arc(cr, radius, radius, radius, G_PI, 3*G_PI/2);
    cairo_close_path(cr);
    cairo_fill(cr);
    
    /* Create region from surface and apply as shape to window */
    cairo_region_t *region = gdk_cairo_region_create_from_surface(surface);
    gtk_widget_shape_combine_region(widget, region);
    
    cairo_region_destroy(region);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    
    /* Force redraw so Cairo shape updates when window shrinks/expands */
    gtk_widget_queue_draw(widget);
    
    GdkDisplay *display = gdk_window_get_display(gdk_window);
    GdkRectangle geometry;

    if (get_primary_monitor_geometry(display, &geometry)) {
        int x = geometry.x + (geometry.width - allocation->width) / 2;
        int y = geometry.y + geometry.height - allocation->height - 2;

        gtk_window_move(GTK_WINDOW(widget), x, y);
    }
}

int main(int argc, char *argv[]) {
    /* Suppress accessibility bus warning */
    g_setenv("GTK_A11Y", "none", TRUE);

    gtk_init(&argc, &argv);
    dock_executable_path = g_file_read_link("/proc/self/exe", NULL);
    if (!dock_executable_path && argc > 0 && argv[0] && argv[0][0] != '\0') {
        dock_executable_path = g_strdup(argv[0]);
    }

    is_wayland_session = TRUE;
    init_wayland_protocols();

    /* Load CSS */
    GtkCssProvider *provider = gtk_css_provider_new();
    GdkDisplay *display = gdk_display_get_default();
    GdkScreen *screen = gdk_display_get_default_screen(display);
    
    gtk_style_context_add_provider_for_screen(screen,
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    GError *error = NULL;
    gchar *css_path = dock_build_resource_path("style.css");
    if (!gtk_css_provider_load_from_path(provider, css_path, &error)) {
        g_warning("Failed to load CSS: %s", error->message);
        g_error_free(error);
    }
    g_free(css_path);
    g_object_unref(provider);

    /* UI Setup */
    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "AetherDock");
    
    /* Get screen dimensions via GdkMonitor (non-deprecated) */
    GdkRectangle primary_geom = {0};
    /* Set as DOCK - explicitly declare as dock window */
    gtk_window_set_type_hint(GTK_WINDOW(main_window), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_window_set_default_size(GTK_WINDOW(main_window), -1, 60);
    if (get_primary_monitor_geometry(display, &primary_geom)) {
        gtk_window_move(GTK_WINDOW(main_window),
                        primary_geom.x + (primary_geom.width / 2),
                        primary_geom.y + primary_geom.height);
    }
    gtk_window_set_gravity(GTK_WINDOW(main_window), GDK_GRAVITY_SOUTH);
    gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);
    gtk_widget_set_app_paintable(main_window, TRUE);
    
    /* Window properties for dock behavior */
    gtk_window_set_keep_above(GTK_WINDOW(main_window), TRUE);
    gtk_window_stick(GTK_WINDOW(main_window));
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(main_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(main_window), TRUE);
    setup_dock_window_layer(main_window);

    /* Enable transparency */
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual != NULL && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(main_window, visual);
    }

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_name(box, "dock-box"); /* ID for CSS */
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    /* Margins removed from C code to rely strictly on window size */
    gtk_container_add(GTK_CONTAINER(main_window), box);

    g_signal_connect(main_window, "draw", G_CALLBACK(on_window_draw), NULL);
    g_signal_connect(main_window, "realize", G_CALLBACK(on_dock_realize), NULL);
    g_signal_connect(main_window, "size-allocate", G_CALLBACK(on_window_size_allocate), NULL);
    g_signal_connect(main_window, "destroy", G_CALLBACK(on_dock_window_destroy), NULL);

    /* Initialize window groups hash table */
    window_groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    launching_apps = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* Load pinned apps */
    load_pinned_apps();

    /* Create right-side buttons (packed with pack_end, so rightmost = launcher) */
    create_launcher_button(box);    /* Rightmost */
    create_trash_button(box);       /* Next to launcher */
    create_drives_area(box);        /* Drives appear to the left of trash */
    system_separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_name(system_separator, "system-separator");
    gtk_widget_set_valign(system_separator, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(system_separator, 6);
    gtk_widget_set_margin_end(system_separator, 8);
    gtk_widget_set_size_request(system_separator, 1, 28);
    gtk_box_pack_end(GTK_BOX(box), system_separator, FALSE, FALSE, 0);
    gtk_widget_show(system_separator);

    /* Initial update */
    update_window_list();

    gtk_widget_show_all(main_window);
    
    gtk_main();

    if (wayland_event_source_id != 0) {
        g_source_remove(wayland_event_source_id);
        wayland_event_source_id = 0;
    }

    if (toplevel_manager) {
        zwlr_foreign_toplevel_manager_v1_stop(toplevel_manager);
        zwlr_foreign_toplevel_manager_v1_destroy(toplevel_manager);
    }

    if (wl_seat_obj) {
        wl_seat_destroy(wl_seat_obj);
    }

    if (wayland_toplevels) {
        g_hash_table_destroy(wayland_toplevels);
    }

    g_free(dock_executable_path);

    return 0;
}

/* Update the list of windows in the panel */
void update_window_list() {
    /* Clear existing buttons (except permanent right-side widgets) */
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(box));
    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        GtkWidget *child = GTK_WIDGET(iter->data);
        /* Preserve: launcher button, trash button, drives area */
        if (child != launcher_button &&
            child != trash_button &&
            child != drives_box &&
            child != system_separator) {
            gtk_widget_destroy(child);
        }
    }
    g_list_free(children);

    /* Clear window lists from groups, but keep pinned apps */
    if (window_groups != NULL) {
        GHashTableIter hash_iter;
        gpointer key, value;
        g_hash_table_iter_init(&hash_iter, window_groups);
        while (g_hash_table_iter_next(&hash_iter, &key, &value)) {
            WindowGroup *group = (WindowGroup *)value;
            
            /* Clear windows list */
            if (group->windows) {
                g_list_free(group->windows);
                group->windows = NULL;
            }
            
            /* Remove unpinned groups */
            if (!group->is_pinned) {
                if (group->icon) {
                    g_object_unref(group->icon);
                }
                if (group->desktop_file_path) {
                    g_free(group->desktop_file_path);
                }
                g_free(group->wm_class);
                g_free(group);
                g_hash_table_iter_remove(&hash_iter);
            }
        }
    }

    if (is_wayland_session) {
        if (wayland_toplevels != NULL) {
            GHashTableIter wayland_iter;
            gpointer wayland_key, wayland_value;

            g_hash_table_iter_init(&wayland_iter, wayland_toplevels);
            while (g_hash_table_iter_next(&wayland_iter, &wayland_key, &wayland_value)) {
                WaylandToplevel *item = (WaylandToplevel *)wayland_value;
                const gchar *app_id = (item->app_id && *item->app_id) ? item->app_id : "unknown";
                WindowGroup *group = g_hash_table_lookup(window_groups, app_id);

                if (group == NULL) {
                    group = g_malloc0(sizeof(WindowGroup));
                    group->wm_class = g_strdup(app_id);
                    group->icon = icon_from_app_id(app_id);
                    group->desktop_file_path = desktop_file_path_from_app_id(app_id);
                    group->active_index = 0;
                    group->is_pinned = pinned_app_contains(app_id);
                    g_hash_table_insert(window_groups, g_strdup(app_id), group);
                }

                group->windows = g_list_append(group->windows, item);
            }
        }

        for (GList *l = pinned_apps; l != NULL; l = l->next) {
            const gchar *pinned_app_id = (const gchar *)l->data;
            gchar *normalized_pinned_app_id = normalize_app_id(pinned_app_id);

            if (g_hash_table_lookup(window_groups, normalized_pinned_app_id) == NULL) {
                WindowGroup *group = g_malloc0(sizeof(WindowGroup));
                group->wm_class = g_strdup(normalized_pinned_app_id);
                group->windows = NULL;
                group->active_index = 0;
                group->is_pinned = TRUE;
                group->desktop_file_path = desktop_file_path_from_app_id(normalized_pinned_app_id);
                group->icon = icon_from_app_id(normalized_pinned_app_id);
                g_hash_table_insert(window_groups, g_strdup(normalized_pinned_app_id), group);
            }

            g_free(normalized_pinned_app_id);
        }

        GHashTableIter hash_iter;
        gpointer key, value;
        g_hash_table_iter_init(&hash_iter, window_groups);

        while (g_hash_table_iter_next(&hash_iter, &key, &value)) {
            WindowGroup *group = (WindowGroup *)value;
            GtkWidget *button = gtk_button_new();
            int window_count = g_list_length(group->windows);

            gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
            group->button = button;

            if (window_count > 0 && group->desktop_file_path) {
                unmark_app_launching(group->desktop_file_path);
            }

            if (window_count > 1) {
                char tooltip[256];
                snprintf(tooltip, sizeof(tooltip), "%s (%d windows)", group->wm_class, window_count);
                gtk_widget_set_tooltip_text(button, tooltip);
            } else if (window_count == 1) {
                WaylandToplevel *item = (WaylandToplevel *)g_list_first(group->windows)->data;
                gtk_widget_set_tooltip_text(button,
                                            (item->title && *item->title) ? item->title : group->wm_class);
            } else {
                gtk_widget_set_tooltip_text(button, group->wm_class);
            }

            GtkWidget *overlay = gtk_overlay_new();

            if (group->icon) {
                GdkPixbuf *rounded = create_rounded_icon_pixbuf(group->icon, 34, 8.0);
                GtkWidget *image = gtk_image_new_from_pixbuf(rounded ? rounded : group->icon);
                gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
                gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
                gtk_container_add(GTK_CONTAINER(overlay), image);
                if (rounded) g_object_unref(rounded);
            } else {
                GtkWidget *label = gtk_label_new("?");
                gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
                gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
                gtk_container_add(GTK_CONTAINER(overlay), label);
            }

            GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_widget_set_name(dot, "indicator-dot");
            gtk_widget_set_size_request(dot, 6, 6);
            gtk_widget_set_halign(dot, GTK_ALIGN_CENTER);
            gtk_widget_set_valign(dot, GTK_ALIGN_END);
            gtk_widget_set_margin_bottom(dot, 2);
            gtk_overlay_add_overlay(GTK_OVERLAY(overlay), dot);
            gtk_container_add(GTK_CONTAINER(button), overlay);

            if (window_count == 0 && is_app_launching(group->desktop_file_path)) {
                attach_launch_ring(button);
            }

            g_object_set_data(G_OBJECT(button), "group", group);
            g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), NULL);
            g_signal_connect(button, "button-press-event", G_CALLBACK(on_button_press), NULL);

            GtkStyleContext *context = gtk_widget_get_style_context(button);
            if (window_count > 0) {
                gtk_style_context_add_class(context, "running-app");
            } else {
                gtk_style_context_add_class(context, "pinned-app");
            }

            gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
        }

        gtk_widget_show_all(box);
        gtk_window_resize(GTK_WINDOW(main_window), 1, 1);
        return;
    }
    gtk_widget_show_all(box);
    
    /* Force main window to recalculate size and shrink if needed */
    gtk_window_resize(GTK_WINDOW(main_window), 1, 1);
}

/* Activate window on click - cycles through grouped windows */
void on_button_clicked(GtkWidget *widget, gpointer data) {
    (void)data; 
    
    WindowGroup *group = (WindowGroup *)g_object_get_data(G_OBJECT(widget), "group");
    if (group == NULL) return;
    
    int window_count = g_list_length(group->windows);
    
    /* 1. Launch Logic for Pinned Apps */
    if (window_count == 0) {
        if (group->desktop_file_path != NULL) {
            mark_app_launching(group->desktop_file_path);
            attach_launch_ring(widget);

            GError *error = NULL;
            if (!app_mgr_launch(group->desktop_file_path, &error)) {
                unmark_app_launching(group->desktop_file_path);
                g_warning("Failed to launch app: %s", error ? error->message : "Unknown error");
                if (error) g_error_free(error);
                update_window_list();
            }
        }
        return;
    }

    if (is_wayland_session) {
        gboolean app_is_active = FALSE;
        WaylandToplevel *target_item = NULL;

        for (GList *l = group->windows; l != NULL; l = l->next) {
            WaylandToplevel *item = (WaylandToplevel *)l->data;
            if (item->state_flags & WAYLAND_TOPLEVEL_STATE_ACTIVATED) {
                app_is_active = TRUE;
                break;
            }
        }

        if (window_count == 1) {
            target_item = (WaylandToplevel *)group->windows->data;
            if (target_item->state_flags & WAYLAND_TOPLEVEL_STATE_ACTIVATED) {
                if (target_item->state_flags & WAYLAND_TOPLEVEL_STATE_MINIMIZED) {
                    zwlr_foreign_toplevel_handle_v1_unset_minimized(target_item->handle);
                } else {
                    zwlr_foreign_toplevel_handle_v1_set_minimized(target_item->handle);
                }
            } else if (wl_seat_obj) {
                if (target_item->state_flags & WAYLAND_TOPLEVEL_STATE_MINIMIZED) {
                    zwlr_foreign_toplevel_handle_v1_unset_minimized(target_item->handle);
                }
                zwlr_foreign_toplevel_handle_v1_activate(target_item->handle, wl_seat_obj);
            }
        } else {
            if (app_is_active) {
                group->active_index = (group->active_index + 1) % window_count;
            }

            GList *window_node = g_list_nth(group->windows, group->active_index);
            if (window_node == NULL) {
                group->active_index = 0;
                window_node = g_list_first(group->windows);
            }

            target_item = window_node ? (WaylandToplevel *)window_node->data : NULL;
            if (target_item != NULL && wl_seat_obj) {
                if (target_item->state_flags & WAYLAND_TOPLEVEL_STATE_MINIMIZED) {
                    zwlr_foreign_toplevel_handle_v1_unset_minimized(target_item->handle);
                }
                zwlr_foreign_toplevel_handle_v1_activate(target_item->handle, wl_seat_obj);
            }
        }

        if (wl_display_conn) {
            wl_display_flush(wl_display_conn);
        }
        return;
    }
}

/* Context menu and pinned apps functions */

/* Load pinned apps from config file */
void load_pinned_apps() {
    gchar *config_dir = g_build_filename(g_get_user_config_dir(), "simple-panel", NULL);
    gchar *config_file = g_build_filename(config_dir, "pinned-apps", NULL);
    
    g_mkdir_with_parents(config_dir, 0755);
    
    GError *error = NULL;
    gchar *contents = NULL;
    
    if (g_file_get_contents(config_file, &contents, NULL, &error)) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        for (int i = 0; lines[i] != NULL; i++) {
            g_strstrip(lines[i]);
            if (strlen(lines[i]) > 0) {
                gchar *normalized = normalize_app_id(lines[i]);
                if (!pinned_app_contains(normalized)) {
                    pinned_apps = g_list_append(pinned_apps, normalized);
                } else {
                    g_free(normalized);
                }
            }
        }
        g_strfreev(lines);
        g_free(contents);
    }
    
    g_free(config_file);
    g_free(config_dir);
}

/* Save pinned apps to config file */
void save_pinned_apps() {
    gchar *config_dir = g_build_filename(g_get_user_config_dir(), "simple-panel", NULL);
    gchar *config_file = g_build_filename(config_dir, "pinned-apps", NULL);
    
    g_mkdir_with_parents(config_dir, 0755);
    
    GString *contents = g_string_new("");
    for (GList *l = pinned_apps; l != NULL; l = l->next) {
        g_string_append(contents, (gchar *)l->data);
        g_string_append_c(contents, '\n');
    }
    
    GError *error = NULL;
    if (!g_file_set_contents(config_file, contents->str, -1, &error)) {
        g_warning("Failed to save pinned apps: %s", error->message);
        g_error_free(error);
    }
    
    g_string_free(contents, TRUE);
    g_free(config_file);
    g_free(config_dir);
}

/* Pin/Unpin clicked */
void on_pin_clicked(GtkWidget *menuitem, gpointer data) {
    (void)menuitem;
    WindowGroup *group = (WindowGroup *)data;
    
    if (group->is_pinned) {
        /* Unpin */
        group->is_pinned = FALSE;
        
        /* Find and remove from pinned list */
        for (GList *l = pinned_apps; l != NULL; l = l->next) {
            if (app_id_equals((const gchar *)l->data, group->wm_class)) {
                g_free(l->data);
                pinned_apps = g_list_delete_link(pinned_apps, l);
                break;
            }
        }
    } else {
        /* Pin */
        group->is_pinned = TRUE;
        if (!pinned_app_contains(group->wm_class)) {
            pinned_apps = g_list_append(pinned_apps, normalize_app_id(group->wm_class));
        }
    }
    
    save_pinned_apps();
    update_window_list();
}

/* New Window clicked */
void on_new_window_clicked(GtkWidget *menuitem, gpointer data) {
    (void)menuitem;
    WindowGroup *group = (WindowGroup *)data;
    
    if (group->desktop_file_path != NULL) {
        GDesktopAppInfo *app_info = g_desktop_app_info_new_from_filename(group->desktop_file_path);
        if (app_info != NULL) {
            GError *error = NULL;
            GdkAppLaunchContext *context = gdk_display_get_app_launch_context(gdk_display_get_default());
            
            if (!g_app_info_launch(G_APP_INFO(app_info), NULL, G_APP_LAUNCH_CONTEXT(context), &error)) {
                g_warning("Failed to launch app: %s", error->message);
                g_error_free(error);
            }
            
            g_object_unref(context);
            g_object_unref(app_info);
        }
    }
}

/* Close All Windows clicked */
void on_close_all_clicked(GtkWidget *menuitem, gpointer data) {
    (void)menuitem;
    WindowGroup *group = (WindowGroup *)data;
    
    for (GList *l = group->windows; l != NULL; l = l->next) {
        WaylandToplevel *item = (WaylandToplevel *)l->data;
        zwlr_foreign_toplevel_handle_v1_close(item->handle);
    }

    if (wl_display_conn) {
        wl_display_flush(wl_display_conn);
    }
}

/* Run with GPU clicked */
void on_run_with_gpu_clicked(GtkWidget *menuitem, gpointer data) {
    (void)menuitem;
    WindowGroup *group = (WindowGroup *)data;
    
    if (group->desktop_file_path != NULL) {
        GDesktopAppInfo *app_info = g_desktop_app_info_new_from_filename(group->desktop_file_path);
        if (app_info != NULL) {
            /* Get the Exec line from desktop file */
            gchar *exec = g_desktop_app_info_get_string(app_info, "Exec");
            
            if (exec != NULL) {
                /* Remove field codes like %U, %F, etc. */
                gchar *clean_exec = g_strdup(exec);
                gchar *percent = g_strstr_len(clean_exec, -1, " %");
                if (percent != NULL) {
                    *percent = '\0';
                }
                
                /* Build command with GPU environment variables (no & needed - async by default) */
                gchar *gpu_command = g_strdup_printf("env DRI_PRIME=1 __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia %s", clean_exec);
                
                GError *error = NULL;
                if (!g_spawn_command_line_async(gpu_command, &error)) {
                    g_warning("Failed to launch with GPU: %s", error->message);
                    g_error_free(error);
                }
                
                g_free(gpu_command);
                g_free(clean_exec);
                g_free(exec);
            }
            
            g_object_unref(app_info);
        }
    }
}

/* Create context menu */
void create_context_menu(WindowGroup *group, GdkEventButton *event) {
    GtkWidget *menu_box;
    GtkWidget *frame_box;
    close_context_menu();

    if (is_wayland_session) {
        GtkWidget *menu = gtk_menu_new();
        GtkStyleContext *context = gtk_widget_get_style_context(menu);

        context_menu_window = menu;
        gtk_widget_set_name(menu, "dock-context-menu");
        gtk_widget_set_app_paintable(menu, TRUE);
        gtk_container_set_border_width(GTK_CONTAINER(menu), 0);
        gtk_menu_set_reserve_toggle_size(GTK_MENU(menu), FALSE);
        gtk_style_context_add_class(context, "dock-context-menu");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                              create_wayland_menu_item(group->is_pinned ? "Unpin from Dock" : "Pin to Dock",
                                                       group, on_pin_clicked));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

        if (group->desktop_file_path != NULL) {
            gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                                  create_wayland_menu_item("New Window", group, on_new_window_clicked));
        }

        if (group->windows != NULL && g_list_length(group->windows) > 0) {
            gchar *label = g_strdup_printf("Close All (%d)", g_list_length(group->windows));
            gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                                  create_wayland_menu_item(label, group, on_close_all_clicked));
            g_free(label);
        }

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

        if (group->desktop_file_path != NULL) {
            gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                                  create_wayland_menu_item("Run with Dedicated GPU", group, on_run_with_gpu_clicked));
        }

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return;
    }

    context_menu_window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_name(context_menu_window, "context-menu-window");
    gtk_widget_set_app_paintable(context_menu_window, TRUE);
    gtk_window_set_decorated(GTK_WINDOW(context_menu_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(context_menu_window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(context_menu_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(context_menu_window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(context_menu_window), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_window_set_accept_focus(GTK_WINDOW(context_menu_window), TRUE);
    gtk_window_set_focus_on_map(GTK_WINDOW(context_menu_window), TRUE);
    gtk_widget_add_events(context_menu_window,
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_FOCUS_CHANGE_MASK);

    {
        GdkScreen *screen = gtk_widget_get_screen(context_menu_window);
        GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
        if (visual && gdk_screen_is_composited(screen)) {
            gtk_widget_set_visual(context_menu_window, visual);
        }
    }

    frame_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(frame_box, "context-menu-frame");
    gtk_container_add(GTK_CONTAINER(context_menu_window), frame_box);

    menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_name(menu_box, "context-menu-box");
    gtk_container_add(GTK_CONTAINER(frame_box), menu_box);

    gtk_box_pack_start(GTK_BOX(menu_box),
                       create_context_menu_item(group->is_pinned ? "Unpin from Dock" : "Pin to Dock",
                                                group, on_pin_clicked),
                       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(menu_box), create_context_menu_separator(), FALSE, FALSE, 4);

    if (group->desktop_file_path != NULL) {
        gtk_box_pack_start(GTK_BOX(menu_box),
                           create_context_menu_item("New Window", group, on_new_window_clicked),
                           FALSE, FALSE, 0);
    }

    if (group->windows != NULL && g_list_length(group->windows) > 0) {
        gchar *label = g_strdup_printf("Close All (%d)", g_list_length(group->windows));
        gtk_box_pack_start(GTK_BOX(menu_box),
                           create_context_menu_item(label, group, on_close_all_clicked),
                           FALSE, FALSE, 0);
        g_free(label);
    }

    gtk_box_pack_start(GTK_BOX(menu_box), create_context_menu_separator(), FALSE, FALSE, 4);

    if (group->desktop_file_path != NULL) {
        gtk_box_pack_start(GTK_BOX(menu_box),
                           create_context_menu_item("Run with Dedicated GPU", group, on_run_with_gpu_clicked),
                           FALSE, FALSE, 0);
    }

    g_signal_connect(context_menu_window, "focus-out-event", G_CALLBACK(on_context_menu_focus_out), NULL);
    g_signal_connect(context_menu_window, "key-press-event", G_CALLBACK(on_context_menu_key_press), NULL);
    g_signal_connect(context_menu_window, "button-press-event", G_CALLBACK(on_context_menu_button_press), NULL);
    g_signal_connect(context_menu_window, "grab-broken-event", G_CALLBACK(on_context_menu_grab_broken), NULL);
    g_signal_connect(context_menu_window, "notify::is-active", G_CALLBACK(on_context_menu_window_active_notify), NULL);
    g_signal_connect(context_menu_window, "destroy", G_CALLBACK(gtk_widget_destroyed), &context_menu_window);

    gtk_widget_show_all(context_menu_window);
    position_context_menu(context_menu_window, event);
    gtk_window_present(GTK_WINDOW(context_menu_window));
    gtk_widget_grab_focus(context_menu_window);

    {
        GdkDisplay *display = gtk_widget_get_display(context_menu_window);
        GdkSeat *seat = gdk_display_get_default_seat(display);
        GdkWindow *window = gtk_widget_get_window(context_menu_window);
        GdkGrabStatus grab_status = GDK_GRAB_NOT_VIEWABLE;

        if (seat && window) {
            grab_status = gdk_seat_grab(seat,
                                        window,
                                        GDK_SEAT_CAPABILITY_ALL_POINTING | GDK_SEAT_CAPABILITY_KEYBOARD,
                                        TRUE,
                                        NULL,
                                        (GdkEvent *)event,
                                        NULL,
                                        NULL);
        }

        if (grab_status == GDK_GRAB_SUCCESS) {
            context_menu_grab_seat = seat;
        }
    }
}

/* Button press event handler */
gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)data;
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { /* Right click */
        WindowGroup *group = (WindowGroup *)g_object_get_data(G_OBJECT(widget), "group");
        if (group != NULL) {
            create_context_menu(group, event);
            return TRUE;
        }
    }
    return FALSE;
}
