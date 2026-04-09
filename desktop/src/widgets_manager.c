/*
 * widgets_manager.c
 * Dynamic widget loading and widget UI.
*/

#include "widgets_manager.h"
#include "desktop_config.h"
#include "wallpaper.h"
#include "icons.h"
#include "vaxp-widget-api.h"
#include <dlfcn.h>
#include <glib/gstdio.h>
#include <string.h>

#define WIDGETS_DIR "/home/x/.config/vaxp/widgets"
#define WIDGETS_ENABLED_CONFIG "/home/x/.config/vaxp/widgets-enabled"

typedef struct {
    void *dl_handle;
    vaxpWidgetAPI *api;
    GtkWidget *root;
    gboolean enabled;
} LoadedWidget;

typedef struct {
    char *fname;
    GtkWidget *toggle;
} WidgetRow;

static GHashTable *loaded_widgets = NULL;
static vaxpDesktopAPI desktop_api;

static char current_widget_bg_color[16] = "#000000";
static double current_widget_bg_opacity = 0.5;

static void update_all_widgets_theme(void) {
    if (!loaded_widgets) return;
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, loaded_widgets);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        LoadedWidget *w = (LoadedWidget *)value;
        if (w && w->api && w->api->update_theme) {
            w->api->update_theme(current_widget_bg_color, current_widget_bg_opacity);
        }
    }
}

static void save_widget_theme_config(void) {
    GKeyFile *kf = g_key_file_new();
    ensure_config_dir();
    g_key_file_load_from_file(kf, "/home/x/.config/vaxp/widgets-theme", G_KEY_FILE_NONE, NULL);
    g_key_file_set_string(kf, "Theme", "Color", current_widget_bg_color);
    g_key_file_set_double(kf, "Theme", "Opacity", current_widget_bg_opacity);
    g_key_file_save_to_file(kf, "/home/x/.config/vaxp/widgets-theme", NULL);
    g_key_file_free(kf);
}

static void load_widget_theme_config(void) {
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, "/home/x/.config/vaxp/widgets-theme", G_KEY_FILE_NONE, NULL)) {
        char *col = g_key_file_get_string(kf, "Theme", "Color", NULL);
        if (col) {
            strncpy(current_widget_bg_color, col, sizeof(current_widget_bg_color) - 1);
            g_free(col);
        }
        GError *err = NULL;
        double op = g_key_file_get_double(kf, "Theme", "Opacity", &err);
        if (!err) current_widget_bg_opacity = op;
        else g_error_free(err);
    }
    g_key_file_free(kf);
    update_all_widgets_theme(); /* In case widgets were already loaded */
}

static void free_signal_data(gpointer data, GClosure *closure) {
    (void)closure;
    g_free(data);
}

static gboolean is_widget_enabled(const char *fname);
static LoadedWidget *get_loaded_widget(const char *fname);
static LoadedWidget *ensure_loaded_widget_entry(const char *fname);
static gboolean widget_load_metadata(const char *fname, gboolean log_errors);
static gboolean widget_ensure_ui(const char *fname, GtkWidget *layout, gboolean log_errors);
static void set_widget_enabled(const char *fname, gboolean enabled);
static gboolean on_widget_toggle_changed(GtkSwitch *sw, gboolean state, gpointer user_data);
static void show_edit_widgets_dialog(GtkWidget *parent_widget);

static void on_widget_root_destroy(GtkWidget *widget, gpointer user_data) {
    LoadedWidget *w = (LoadedWidget *)user_data;

    if (!w) return;
    if (w->root == widget) w->root = NULL;
}

static void loaded_widget_free(gpointer ptr) {
    LoadedWidget *w = (LoadedWidget *)ptr;
    if (!w) return;
    g_free(w);
}

static void ensure_widget_registry(void) {
    if (loaded_widgets) return;
    loaded_widgets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, loaded_widget_free);
}

static LoadedWidget *get_loaded_widget(const char *fname) {
    if (!loaded_widgets || !fname) return NULL;
    return (LoadedWidget *)g_hash_table_lookup(loaded_widgets, fname);
}

static LoadedWidget *ensure_loaded_widget_entry(const char *fname) {
    LoadedWidget *w;

    ensure_widget_registry();
    w = get_loaded_widget(fname);
    if (w) return w;

    w = g_new0(LoadedWidget, 1);
    g_hash_table_insert(loaded_widgets, g_strdup(fname), w);
    return w;
}

static gboolean widget_load_metadata(const char *fname, gboolean log_errors) {
    LoadedWidget *w = ensure_loaded_widget_entry(fname);
    char *full_path;
    void *handle;
    vaxpWidgetAPI *(*init_func)(void);
    vaxpWidgetAPI *api;

    if (w->api) return TRUE;

    full_path = g_strdup_printf("%s/%s", WIDGETS_DIR, fname);
    handle = dlopen(full_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        if (log_errors) g_warning("[Widgets] Failed to load %s: %s", fname, dlerror());
        g_free(full_path);
        return FALSE;
    }

    init_func = dlsym(handle, "vaxp_widget_init");
    if (!init_func) {
        if (log_errors) g_warning("[Widgets] Missing 'vaxp_widget_init' in %s", fname);
        dlclose(handle);
        g_free(full_path);
        return FALSE;
    }

    api = init_func();
    if (!api || !api->create_widget) {
        if (log_errors) g_warning("[Widgets] Invalid API from %s (missing create_widget)", fname);
        dlclose(handle);
        g_free(full_path);
        return FALSE;
    }

    w->dl_handle = handle;
    w->api = api;
    g_free(full_path);
    return TRUE;
}

void apply_widget_visibility(GtkWidget *root) {
    DesktopMode active_mode;
    const char *fname;
    gboolean enabled = TRUE;
    LoadedWidget *w;

    if (!root) return;

    active_mode = get_current_desktop_mode();
    fname = (const char *)g_object_get_data(G_OBJECT(root), "vaxp-widget-fname");
    if (fname) {
        w = get_loaded_widget(fname);
        enabled = is_widget_enabled(fname);
        if (w) w->enabled = enabled;
    }

    if (active_mode == MODE_WIDGETS && enabled) gtk_widget_show(root);
    else gtk_widget_hide(root);
}

static gboolean widget_ensure_ui(const char *fname, GtkWidget *layout, gboolean log_errors) {
    LoadedWidget *w;
    GtkWidget *ui;
    int x = 100;
    int y = 100;

    if (!widget_load_metadata(fname, log_errors)) return FALSE;

    w = get_loaded_widget(fname);
    if (!w || !w->api) return FALSE;
    if (w->root) {
        apply_widget_visibility(w->root);
        return TRUE;
    }

    ui = w->api->create_widget(&desktop_api);
    if (!ui) return FALSE;

    w->root = ui;
    gtk_widget_set_name(ui, "vaxp-widget");
    g_object_set_data_full(G_OBJECT(ui), "vaxp-widget-fname", g_strdup(fname), g_free);
    g_signal_connect(ui, "destroy", G_CALLBACK(on_widget_root_destroy), w);

    get_item_position(fname, &x, &y);
    gtk_layout_put(GTK_LAYOUT(layout), ui, x, y);
    gtk_widget_show_all(ui);

    g_print("[Widgets] Successfully loaded '%s' V1.0 by %s\n",
            w->api->name ? w->api->name : fname,
            w->api->author ? w->api->author : "Unknown");

    apply_widget_visibility(ui);
    return TRUE;
}

static gboolean is_widget_enabled(const char *fname) {
    char *contents = NULL;
    gsize len = 0;
    gchar **lines;

    if (!g_file_get_contents(WIDGETS_ENABLED_CONFIG, &contents, &len, NULL))
        return TRUE;

    lines = g_strsplit(contents, "\n", -1);
    g_free(contents);

    for (int i = 0; lines[i] != NULL; i++) {
        g_strstrip(lines[i]);
        if (g_str_has_prefix(lines[i], "disabled:")) {
            const char *disabled_name = lines[i] + 9;
            if (g_strcmp0(disabled_name, fname) == 0) {
                g_strfreev(lines);
                return FALSE;
            }
        }
    }

    g_strfreev(lines);
    return TRUE;
}

static void set_widget_enabled(const char *fname, gboolean enabled) {
    char *contents = NULL;
    gsize len = 0;
    GString *new_contents;
    gboolean found = FALSE;

    ensure_config_dir();
    g_file_get_contents(WIDGETS_ENABLED_CONFIG, &contents, &len, NULL);
    new_contents = g_string_new("");

    if (contents) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        g_free(contents);

        for (int i = 0; lines[i] != NULL; i++) {
            g_strstrip(lines[i]);
            if (strlen(lines[i]) == 0) continue;

            if (g_str_has_prefix(lines[i], "disabled:")) {
                const char *disabled_name = lines[i] + 9;
                if (g_strcmp0(disabled_name, fname) == 0) {
                    found = TRUE;
                    if (!enabled) {
                        g_string_append_printf(new_contents, "disabled:%s\n", fname);
                    }
                    continue;
                }
            }

            g_string_append_printf(new_contents, "%s\n", lines[i]);
        }

        g_strfreev(lines);
    }

    if (!found && !enabled) {
        g_string_append_printf(new_contents, "disabled:%s\n", fname);
    }

    g_file_set_contents(WIDGETS_ENABLED_CONFIG, new_contents->str, -1, NULL);
    g_string_free(new_contents, TRUE);
}

void load_all_widgets(GtkWidget *layout) {
    GDir *dir;
    const char *fname;

    load_widget_theme_config();

    g_mkdir_with_parents(WIDGETS_DIR, 0755);
    dir = g_dir_open(WIDGETS_DIR, 0, NULL);
    if (!dir) return;

    ensure_widget_registry();
    desktop_api.layout_container = layout;
    desktop_api.save_position = save_item_position;

    while ((fname = g_dir_read_name(dir))) {
        LoadedWidget *w;

        if (!g_str_has_suffix(fname, ".so")) continue;

        w = ensure_loaded_widget_entry(fname);
        w->enabled = is_widget_enabled(fname);
        if (!w->enabled) {
            g_print("[Widgets] Skipping disabled widget '%s'\n", fname);
            continue;
        }

        if (widget_ensure_ui(fname, layout, TRUE)) {
            if (w->api && w->api->update_theme) {
                w->api->update_theme(current_widget_bg_color, current_widget_bg_opacity);
            }
        }
    }

    g_dir_close(dir);
}

void reload_widgets(void) {
    GDir *dir;
    const char *fname;
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    g_mkdir_with_parents(WIDGETS_DIR, 0755);
    dir = g_dir_open(WIDGETS_DIR, 0, NULL);
    if (!dir) return;

    ensure_widget_registry();
    desktop_api.layout_container = icon_layout;
    desktop_api.save_position = save_item_position;

    while ((fname = g_dir_read_name(dir))) {
        LoadedWidget *w;

        if (!g_str_has_suffix(fname, ".so")) continue;

        w = ensure_loaded_widget_entry(fname);
        w->enabled = is_widget_enabled(fname);
        if (w->enabled) {
            widget_ensure_ui(fname, icon_layout, TRUE);
        } else if (w->root) {
            gtk_widget_hide(w->root);
        }
    }
    g_dir_close(dir);

    g_hash_table_iter_init(&iter, loaded_widgets);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const char *loaded_fname = (const char *)key;
        LoadedWidget *w = (LoadedWidget *)value;
        w->enabled = is_widget_enabled(loaded_fname);
        if (w->root) {
            apply_widget_visibility(w->root);
            if (w->enabled && w->api && w->api->update_theme) {
                w->api->update_theme(current_widget_bg_color, current_widget_bg_opacity);
            }
        }
    }
}

void on_mode_normal(GtkWidget *item, gpointer data) {
    (void)item;
    (void)data;
    set_current_desktop_mode(MODE_NORMAL);
    refresh_icons();
}

void on_mode_work(GtkWidget *item, gpointer data) {
    (void)item;
    (void)data;
    set_current_desktop_mode(MODE_WORK);
    refresh_icons();
}

void on_mode_widgets(GtkWidget *item, gpointer data) {
    (void)item;
    (void)data;
    set_current_desktop_mode(MODE_WIDGETS);
    refresh_icons();
}

static gboolean on_widget_toggle_changed(GtkSwitch *sw, gboolean state, gpointer user_data) {
    (void)sw;
    set_widget_enabled((const char *)user_data, state);
    return FALSE;
}

static void on_theme_color_set(GtkColorButton *btn, gpointer user_data) {
    (void)user_data;
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &rgba);
    snprintf(current_widget_bg_color, sizeof(current_widget_bg_color), "#%02X%02X%02X",
             (int)(rgba.red * 255), (int)(rgba.green * 255), (int)(rgba.blue * 255));
    update_all_widgets_theme();
    save_widget_theme_config();
}

static void on_theme_opacity_changed(GtkRange *range, gpointer user_data) {
    (void)user_data;
    current_widget_bg_opacity = gtk_range_get_value(range);
    update_all_widgets_theme();
    save_widget_theme_config();
}

static void show_edit_widgets_dialog(GtkWidget *parent_widget) {
    GtkWidget *dialog;
    GdkScreen *dlg_screen;
    GdkVisual *dlg_visual;
    GtkCssProvider *dlg_css;
    GtkWidget *content;
    GtkWidget *header_lbl;
    GtkWidget *scroll;
    GtkWidget *list_box;
    GList *rows = NULL;
    GDir *dir;
    int widget_count = 0;

    (void)parent_widget;

    dialog = gtk_dialog_new_with_buttons(
        "Edit Widgets",
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Apply", GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 480, 380);

    dlg_screen = gtk_widget_get_screen(dialog);
    dlg_visual = gdk_screen_get_rgba_visual(dlg_screen);
    if (dlg_visual && gdk_screen_is_composited(dlg_screen)) {
        gtk_widget_set_visual(dialog, dlg_visual);
        gtk_widget_set_app_paintable(dialog, TRUE);
    }

    dlg_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(dlg_css,
        "window, dialog { background-color: transparent; }"
        ".edit-widgets-dialog { background-color: rgba(0, 0, 0, 0.0); }"
        ".widget-row { background-color: rgba(255,255,255,0.05);"
        "  border: 1px solid rgba(0,252,210,0.15); border-radius: 8px;"
        "  padding: 10px 14px; margin: 4px 0px; }"
        ".widget-row:hover { background-color: rgba(0,252,210,0.10);"
        "  border-color: rgba(0,252,210,0.5); }"
        ".widget-name { color: white; font-size: 13px; font-weight: bold; }"
        ".widget-file { color: rgba(255,255,255,0.45); font-size: 11px; }"
        ".header-label { color: rgb(0,252,210); font-size: 12px; font-weight: bold;"
        "  padding: 4px 0px 8px 0px; }"
        "switch:checked { background-color: rgb(0,252,210); }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        dlg_screen,
        GTK_STYLE_PROVIDER(dlg_css),
        GTK_STYLE_PROVIDER_PRIORITY_USER);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(content), "edit-widgets-dialog");

    header_lbl = gtk_label_new("Installed Widgets — toggle to show/hide on desktop");
    gtk_widget_set_halign(header_lbl, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(header_lbl), "header-label");
    gtk_box_pack_start(GTK_BOX(content), header_lbl, FALSE, FALSE, 0);

    GtkWidget *theme_frame = gtk_frame_new("Widget Theme (API-driven)");
    gtk_widget_set_margin_bottom(theme_frame, 8);
    GtkWidget *theme_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(theme_vbox), 8);
    gtk_container_add(GTK_CONTAINER(theme_frame), theme_vbox);
    
    GtkWidget *color_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(color_box), gtk_label_new("Background Color:"), FALSE, FALSE, 0);
    GtkWidget *color_btn = gtk_color_button_new();
    GdkRGBA current_rgba;
    gdk_rgba_parse(&current_rgba, current_widget_bg_color);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color_btn), &current_rgba);
    gtk_box_pack_start(GTK_BOX(color_box), color_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(theme_vbox), color_box, FALSE, FALSE, 0);
    
    GtkWidget *op_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(op_box), gtk_label_new("Opacity:"), FALSE, FALSE, 0);
    GtkWidget *op_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.05);
    gtk_range_set_value(GTK_RANGE(op_scale), current_widget_bg_opacity);
    gtk_widget_set_hexpand(op_scale, TRUE);
    gtk_box_pack_start(GTK_BOX(op_box), op_scale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(theme_vbox), op_box, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(content), theme_frame, FALSE, FALSE, 0);
    
    g_signal_connect(color_btn, "color-set", G_CALLBACK(on_theme_color_set), NULL);
    g_signal_connect(op_scale, "value-changed", G_CALLBACK(on_theme_opacity_changed), NULL);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);

    list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), list_box);

    g_mkdir_with_parents(WIDGETS_DIR, 0755);
    dir = g_dir_open(WIDGETS_DIR, 0, NULL);

    if (dir) {
        const char *fname;

        while ((fname = g_dir_read_name(dir))) {
            LoadedWidget *w;
            const char *display_name;
            const char *author_str;
            GtkWidget *row_box;
            GtkWidget *icon;
            GtkWidget *vbox;
            GtkWidget *name_lbl;
            GtkWidget *file_lbl;
            GtkWidget *toggle;
            char *file_info;
            char *fname_copy;

            if (!g_str_has_suffix(fname, ".so")) continue;
            widget_count++;

            widget_load_metadata(fname, FALSE);
            w = get_loaded_widget(fname);
            display_name = (w && w->api && w->api->name) ? w->api->name : fname;
            author_str = (w && w->api) ? w->api->author : NULL;

            row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_style_context_add_class(gtk_widget_get_style_context(row_box), "widget-row");
            gtk_widget_set_margin_start(row_box, 4);
            gtk_widget_set_margin_end(row_box, 4);

            icon = gtk_image_new_from_icon_name("preferences-desktop-default-applications",
                                                GTK_ICON_SIZE_DND);
            gtk_box_pack_start(GTK_BOX(row_box), icon, FALSE, FALSE, 0);

            vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            gtk_widget_set_hexpand(vbox, TRUE);

            name_lbl = gtk_label_new(display_name);
            gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
            gtk_style_context_add_class(gtk_widget_get_style_context(name_lbl), "widget-name");
            gtk_box_pack_start(GTK_BOX(vbox), name_lbl, FALSE, FALSE, 0);

            file_info = author_str
                ? g_strdup_printf("%s  ·  by %s", fname, author_str)
                : g_strdup(fname);
            file_lbl = gtk_label_new(file_info);
            g_free(file_info);
            gtk_widget_set_halign(file_lbl, GTK_ALIGN_START);
            gtk_style_context_add_class(gtk_widget_get_style_context(file_lbl), "widget-file");
            gtk_box_pack_start(GTK_BOX(vbox), file_lbl, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(row_box), vbox, TRUE, TRUE, 0);

            toggle = gtk_switch_new();
            gtk_switch_set_active(GTK_SWITCH(toggle), is_widget_enabled(fname));
            gtk_widget_set_valign(toggle, GTK_ALIGN_CENTER);
            fname_copy = g_strdup(fname);
            g_signal_connect_data(toggle, "state-set",
                                  G_CALLBACK(on_widget_toggle_changed),
                                  fname_copy, free_signal_data, 0);
            gtk_box_pack_end(GTK_BOX(row_box), toggle, FALSE, FALSE, 0);

            gtk_list_box_insert(GTK_LIST_BOX(list_box), row_box, -1);
            rows = g_list_append(rows, row_box);
        }
        g_dir_close(dir);
    }

    if (widget_count == 0) {
        GtkWidget *empty = gtk_label_new("No widgets found in ~/.config/vaxp/widgets/");
        gtk_widget_set_sensitive(empty, FALSE);
        gtk_box_pack_start(GTK_BOX(content), empty, TRUE, TRUE, 20);
    }

    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        reload_widgets();
    }

    gtk_widget_destroy(dialog);
    g_object_unref(dlg_css);
    g_list_free(rows);
}

void on_edit_widgets(GtkWidget *item, gpointer data) {
    (void)item;
    show_edit_widgets_dialog(GTK_WIDGET(data));
}
