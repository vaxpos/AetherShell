/*
 * wallpaper.c
 * Window bootstrap, wallpaper rendering, and wallpaper picker UI.
 */

#include "wallpaper.h"
#include "desktop_config.h"
#include <glib/gstdio.h>
#include <string.h>
#include <gtk-layer-shell.h>

#define WALLPAPER_CONFIG_FILE "/home/x/.config/vaxp/wallpaper"
#define WALLPAPER_DIR "/usr/share/backgrounds"
#define WALLPAPER_DIRS_CONFIG "/home/x/.config/vaxp/wallpaper-dirs"

GtkWidget *main_window = NULL;
GtkWidget *icon_layout = NULL;
int screen_w = 0;
int screen_h = 0;

static GdkPixbuf *wallpaper_pixbuf = NULL;
static char *current_wallpaper_path = NULL;
static gboolean monitor_signal_handlers_connected = FALSE;

static void load_wallpaper(const char *path);
static GdkMonitor *get_target_monitor(GdkDisplay *display);
static void update_desktop_geometry(void);
static void on_monitors_changed(GdkDisplay *display, gpointer user_data);
static void on_main_window_realize(GtkWidget *widget, gpointer user_data);
static gboolean refresh_desktop_after_show(gpointer user_data);
static gboolean is_image_file(const char *name);
static void add_images_from_dir(const char *dir_path, GtkWidget *flow);
static void on_browse_folder_clicked(GtkButton *btn, gpointer user_data);

static void load_wallpaper(const char *path) {
    GError *err = NULL;
    GdkPixbuf *pb;

    if (!path || strlen(path) == 0) return;
    if (screen_w <= 0 || screen_h <= 0) return;

    pb = gdk_pixbuf_new_from_file_at_scale(path, screen_w, screen_h, FALSE, &err);
    if (!pb) {
        g_warning("[Wallpaper] Failed to load '%s': %s", path, err ? err->message : "?");
        if (err) g_error_free(err);
        return;
    }

    if (wallpaper_pixbuf) g_object_unref(wallpaper_pixbuf);
    wallpaper_pixbuf = pb;

    g_free(current_wallpaper_path);
    current_wallpaper_path = g_strdup(path);

    ensure_config_dir();
    g_file_set_contents(WALLPAPER_CONFIG_FILE, path, -1, NULL);

    if (icon_layout) gtk_widget_queue_draw(icon_layout);
}

void load_saved_wallpaper(void) {
    char *path = NULL;
    gsize len = 0;
    gboolean valid;

    if (!g_file_get_contents(WALLPAPER_CONFIG_FILE, &path, &len, NULL))
        return;

    g_strstrip(path);

    valid = (path[0] == '/');
    for (gsize i = 1; valid && i < strlen(path); i++) {
        if ((unsigned char)path[i] < 0x20 || (unsigned char)path[i] > 0x7E) {
            valid = FALSE;
        }
    }

    if (valid && strlen(path) > 1) load_wallpaper(path);
    else g_warning("[Wallpaper] Saved path is invalid, ignoring: '%s'", path);

    g_free(path);
}

static GdkMonitor *get_target_monitor(GdkDisplay *display) {
    GdkMonitor *monitor;

    if (!display) return NULL;

    monitor = gdk_display_get_primary_monitor(display);
    if (monitor) return monitor;

    if (gdk_display_get_n_monitors(display) > 0)
        return gdk_display_get_monitor(display, 0);

    return NULL;
}

gboolean desktop_has_available_monitor(void) {
    GdkDisplay *display = gdk_display_get_default();

    if (!display) return FALSE;
    return get_target_monitor(display) != NULL;
}

static void update_desktop_geometry(void) {
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor;
    GdkRectangle r;

    if (!display || !main_window || !icon_layout) return;

    monitor = get_target_monitor(display);
    if (!monitor) return;

    gdk_monitor_get_geometry(monitor, &r);
    if (r.width <= 0 || r.height <= 0) return;

    screen_w = r.width;
    screen_h = r.height;

    gtk_layer_set_monitor(GTK_WINDOW(main_window), monitor);
    gtk_window_set_default_size(GTK_WINDOW(main_window), screen_w, screen_h);
    gtk_window_resize(GTK_WINDOW(main_window), screen_w, screen_h);
    gtk_widget_set_size_request(icon_layout, screen_w, screen_h);
    gtk_layout_set_size(GTK_LAYOUT(icon_layout), screen_w, screen_h);

    if (current_wallpaper_path) load_wallpaper(current_wallpaper_path);
}

static void on_monitors_changed(GdkDisplay *display, gpointer user_data) {
    (void)display;
    (void)user_data;
    update_desktop_geometry();
}

static void on_main_window_realize(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;
    update_desktop_geometry();
    g_idle_add(refresh_desktop_after_show, NULL);
}

static gboolean refresh_desktop_after_show(gpointer user_data) {
    (void)user_data;
    update_desktop_geometry();
    if (icon_layout) gtk_widget_queue_draw(icon_layout);
    return G_SOURCE_REMOVE;
}

gboolean on_layout_draw_bg(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)widget;
    (void)data;

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (wallpaper_pixbuf) {
        gdk_cairo_set_source_pixbuf(cr, wallpaper_pixbuf, 0, 0);
        cairo_paint(cr);
    }

    return FALSE;
}

void init_main_window(void) {
    GdkVisual *visual;
    GdkScreen *screen;

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "vaxp Pro Desktop");
    gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(main_window), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(main_window), FALSE);

    gtk_layer_init_for_window(GTK_WINDOW(main_window));
    gtk_layer_set_namespace(GTK_WINDOW(main_window), "desktop");
    gtk_layer_set_layer(GTK_WINDOW(main_window), GTK_LAYER_SHELL_LAYER_BACKGROUND);
    gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(main_window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(main_window), -1);

    screen = gtk_widget_get_screen(main_window);
    visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(main_window, visual);
        gtk_widget_set_app_paintable(main_window, TRUE);
    }

    icon_layout = gtk_layout_new(NULL, NULL);
    gtk_widget_set_app_paintable(icon_layout, TRUE);
    gtk_widget_add_events(icon_layout,
        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

    if (!monitor_signal_handlers_connected) {
        GdkDisplay *display = gdk_display_get_default();

        if (display) {
            g_signal_connect(display, "monitor-added", G_CALLBACK(on_monitors_changed), NULL);
            g_signal_connect(display, "monitor-removed", G_CALLBACK(on_monitors_changed), NULL);
            monitor_signal_handlers_connected = TRUE;
        }
    }

    g_signal_connect(main_window, "realize", G_CALLBACK(on_main_window_realize), NULL);
    g_signal_connect(icon_layout, "draw", G_CALLBACK(on_layout_draw_bg), NULL);

    gtk_container_add(GTK_CONTAINER(main_window), icon_layout);
    update_desktop_geometry();
}

static gboolean is_image_file(const char *name) {
    const char *exts[] = { ".jpg", ".jpeg", ".png", ".bmp", ".gif", ".webp", ".tiff", ".svg", NULL };
    char *lower = g_ascii_strdown(name, -1);
    gboolean ok = FALSE;

    for (int i = 0; exts[i]; i++) {
        if (g_str_has_suffix(lower, exts[i])) {
            ok = TRUE;
            break;
        }
    }

    g_free(lower);
    return ok;
}

static void add_images_from_dir(const char *dir_path, GtkWidget *flow) {
    GDir *dir = g_dir_open(dir_path, 0, NULL);
    const char *fname;

    if (!dir) return;

    while ((fname = g_dir_read_name(dir))) {
        char *full_path;
        GdkPixbuf *thumb;
        GtkWidget *img;
        GtkWidget *vbox;
        GtkWidget *lbl;

        if (!is_image_file(fname)) continue;

        full_path = g_strdup_printf("%s/%s", dir_path, fname);
        thumb = gdk_pixbuf_new_from_file_at_scale(full_path, 180, 110, FALSE, NULL);
        img = thumb
            ? gtk_image_new_from_pixbuf(thumb)
            : gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_DIALOG);
        if (thumb) g_object_unref(thumb);

        vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_box_pack_start(GTK_BOX(vbox), img, FALSE, FALSE, 0);

        lbl = gtk_label_new(fname);
        gtk_label_set_max_width_chars(GTK_LABEL(lbl), 22);
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);

        g_object_set_data_full(G_OBJECT(vbox), "wallpaper-path", full_path, g_free);
        gtk_flow_box_insert(GTK_FLOW_BOX(flow), vbox, -1);
    }

    g_dir_close(dir);
    gtk_widget_show_all(flow);
}

static void on_browse_folder_clicked(GtkButton *btn, gpointer user_data) {
    GtkWidget *chooser;
    GtkWidget *flow = GTK_WIDGET(user_data);

    (void)btn;

    chooser = gtk_file_chooser_dialog_new(
        "Select Wallpaper Folder",
        GTK_WINDOW(main_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(chooser), 600, 400);

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (folder) {
            char *existing = NULL;
            gboolean already_saved;

            add_images_from_dir(folder, flow);

            g_file_get_contents(WALLPAPER_DIRS_CONFIG, &existing, NULL, NULL);
            already_saved = existing && strstr(existing, folder) != NULL;
            if (!already_saved) {
                GString *buf = g_string_new(existing ? existing : "");
                if (buf->len > 0 && buf->str[buf->len - 1] != '\n')
                    g_string_append_c(buf, '\n');
                g_string_append_printf(buf, "%s\n", folder);
                ensure_config_dir();
                g_file_set_contents(WALLPAPER_DIRS_CONFIG, buf->str, -1, NULL);
                g_string_free(buf, TRUE);
            }

            g_free(existing);
            g_free(folder);
        }
    }

    gtk_widget_destroy(chooser);
}

void show_wallpaper_picker(GtkWidget *parent_widget) {
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *toolbar;
    GtkWidget *lbl_path;
    GtkWidget *browse_btn;
    GtkWidget *scroll;
    GtkWidget *flow;

    (void)parent_widget;

    dialog = gtk_dialog_new_with_buttons(
        "Change Wallpaper",
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Apply", GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 800, 540);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 8);

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_bottom(toolbar, 6);
    gtk_box_pack_start(GTK_BOX(content), toolbar, FALSE, FALSE, 0);

    lbl_path = gtk_label_new("Showing: " WALLPAPER_DIR);
    gtk_widget_set_halign(lbl_path, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(toolbar), lbl_path, TRUE, TRUE, 0);

    browse_btn = gtk_button_new_with_label("📁 Add Custom Folder");
    gtk_box_pack_end(GTK_BOX(toolbar), browse_btn, FALSE, FALSE, 0);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);

    flow = gtk_flow_box_new();
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_SINGLE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_widget_set_margin_start(flow, 4);
    gtk_widget_set_margin_end(flow, 4);
    gtk_container_add(GTK_CONTAINER(scroll), flow);

    g_signal_connect(browse_btn, "clicked", G_CALLBACK(on_browse_folder_clicked), flow);

    add_images_from_dir(WALLPAPER_DIR, flow);

    {
        char *dirs_content = NULL;
        if (g_file_get_contents(WALLPAPER_DIRS_CONFIG, &dirs_content, NULL, NULL)) {
            gchar **lines = g_strsplit(dirs_content, "\n", -1);
            for (int i = 0; lines[i] != NULL; i++) {
                g_strstrip(lines[i]);
                if (strlen(lines[i]) > 1 && lines[i][0] == '/') {
                    add_images_from_dir(lines[i], flow);
                }
            }
            g_strfreev(lines);
            g_free(dirs_content);
        }
    }

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GList *selected = gtk_flow_box_get_selected_children(GTK_FLOW_BOX(flow));
        if (selected) {
            GtkFlowBoxChild *child = GTK_FLOW_BOX_CHILD(selected->data);
            GtkWidget *box = gtk_bin_get_child(GTK_BIN(child));
            const char *path = g_object_get_data(G_OBJECT(box), "wallpaper-path");
            if (path) load_wallpaper(path);
            g_list_free(selected);
        }
    }

    gtk_widget_destroy(dialog);
}
