/*
 * icons.c
 * Icon management, sorting, placement, and drag-and-drop.
 */

#include "icons.h"
#include "filesystem.h"
#include "menu.h"
#include "selection.h"
#include "wallpaper.h"
#include "widgets_manager.h"
#include <gio/gdesktopappinfo.h>
#include <string.h>

double drag_start_x_root = 0;
double drag_start_y_root = 0;
GHashTable *drag_initial_positions = NULL;

static void free_signal_data(gpointer data, GClosure *closure) {
    (void)closure;
    g_free(data);
}

void on_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer data) {
    GtkWidget *box = gtk_bin_get_child(GTK_BIN(widget));

    (void)data;

    if (box) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(box));
        GtkWidget *image = NULL;

        for (GList *l = children; l != NULL; l = l->next) {
            if (GTK_IS_IMAGE(l->data)) {
                image = GTK_WIDGET(l->data);
                break;
            }
        }

        if (image && gtk_image_get_storage_type(GTK_IMAGE(image)) == GTK_IMAGE_PIXBUF) {
            GdkPixbuf *pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(image));
            if (pixbuf) gtk_drag_set_icon_pixbuf(context, pixbuf, 24, 24);
            else gtk_drag_set_icon_default(context);
        } else {
            gtk_drag_set_icon_default(context);
        }

        g_list_free(children);
    } else {
        gtk_drag_set_icon_default(context);
    }
}

void on_drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *data, guint info, guint time, gpointer user_data) {
    GString *uri_list = g_string_new("");

    (void)context;
    (void)info;
    (void)time;
    (void)user_data;

    if (is_selected(widget)) {
        for (GList *l = selected_items; l != NULL; l = l->next) {
            GtkWidget *item = GTK_WIDGET(l->data);
            char *uri = (char *)g_object_get_data(G_OBJECT(item), "uri");
            if (uri) g_string_append_printf(uri_list, "%s\r\n", uri);
        }
    } else {
        char *uri = (char *)g_object_get_data(G_OBJECT(widget), "uri");
        if (uri) g_string_append_printf(uri_list, "%s\r\n", uri);
    }

    gtk_selection_data_set(data, gtk_selection_data_get_target(data), 8,
                           (guchar *)uri_list->str, uri_list->len);
    g_string_free(uri_list, TRUE);
}

void on_folder_drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data, guint info, guint time, gpointer user_data) {
    char *folder_uri = (char *)user_data;
    GFile *folder = g_file_new_for_uri(folder_uri);
    char *folder_path = g_file_get_path(folder);

    (void)widget;
    (void)x;
    (void)y;
    (void)info;

    if (folder_path) {
        gchar **uris = g_uri_list_extract_uris((const gchar *)gtk_selection_data_get_data(data));
        if (uris) {
            for (int i = 0; uris[i] != NULL; i++) {
                GFile *src = g_file_new_for_uri(uris[i]);
                char *basename = g_file_get_basename(src);
                char *dest_path = g_strdup_printf("%s/%s", folder_path, basename);
                GFile *dest = g_file_new_for_path(dest_path);
                GError *err = NULL;

                recursive_copy_move(src, dest, TRUE, &err);
                if (err) {
                    g_warning("DnD Move failed: %s", err->message);
                    g_error_free(err);
                }

                g_free(basename);
                g_free(dest_path);
                g_object_unref(src);
                g_object_unref(dest);
            }
            g_strfreev(uris);
            refresh_icons();
        }
        g_free(folder_path);
    }

    g_object_unref(folder);
    gtk_drag_finish(context, TRUE, FALSE, time);
}

void on_bg_drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data, guint info, guint time, gpointer user_data) {
    GtkWidget *source_widget = gtk_drag_get_source_widget(context);
    char *desktop_path;

    (void)widget;
    (void)info;
    (void)user_data;

    if (source_widget && GTK_IS_WIDGET(source_widget) &&
        g_strcmp0(gtk_widget_get_name(source_widget), "desktop-item") == 0) {
        double delta_x = x - drag_start_x_root;
        double delta_y = y - drag_start_y_root;

        if (drag_initial_positions) {
            GHashTableIter iter;
            gpointer key;
            gpointer value;

            g_hash_table_iter_init(&iter, drag_initial_positions);
            while (g_hash_table_iter_next(&iter, &key, &value)) {
                GtkWidget *item = GTK_WIDGET(key);
                int *start_pos = (int *)value;
                int new_x = start_pos[0] + delta_x;
                int new_y = start_pos[1] + delta_y;

                if (new_x < 0) new_x = 0;
                if (new_y < 0) new_y = 0;
                if (new_x > screen_w - ITEM_WIDTH) new_x = screen_w - ITEM_WIDTH;
                if (new_y > screen_h - ITEM_HEIGHT) new_y = screen_h - ITEM_HEIGHT;

                gtk_layout_move(GTK_LAYOUT(icon_layout), item, new_x, new_y);

                {
                    char *uri = (char *)g_object_get_data(G_OBJECT(item), "uri");
                    if (uri) {
                        GFile *f = g_file_new_for_uri(uri);
                        char *filename = g_file_get_basename(f);
                        save_item_position(filename, new_x, new_y);
                        g_free(filename);
                        g_object_unref(f);
                    }
                }
            }
        }

        gtk_drag_finish(context, TRUE, FALSE, time);
        return;
    }

    desktop_path = get_current_desktop_path();
    if (!desktop_path) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    {
        gchar **uris = g_uri_list_extract_uris((const gchar *)gtk_selection_data_get_data(data));
        if (uris) {
            for (int i = 0; uris[i] != NULL; i++) {
                GFile *src = g_file_new_for_uri(uris[i]);
                char *basename = g_file_get_basename(src);
                char *dest_path = g_strdup_printf("%s/%s", desktop_path, basename);
                GFile *dest = g_file_new_for_path(dest_path);
                GError *err = NULL;

                recursive_copy_move(src, dest, FALSE, &err);
                if (err) {
                    g_warning("Import failed: %s", err->message);
                    g_error_free(err);
                }

                g_free(basename);
                g_free(dest_path);
                g_object_unref(src);
                g_object_unref(dest);
            }
            g_strfreev(uris);
            refresh_icons();
        }
    }

    g_free(desktop_path);
    gtk_drag_finish(context, TRUE, FALSE, time);
}

GtkWidget *create_desktop_item(GFileInfo *info, const char *full_path) {
    const char *filename = g_file_info_get_name(info);
    char *display_name = g_strdup(g_file_info_get_display_name(info));
    GIcon *gicon = g_object_ref(g_file_info_get_icon(info));
    gboolean is_dir = (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY);
    GtkWidget *btn;
    GtkWidget *box;
    GtkWidget *image;
    GtkWidget *label;
    GFile *f;
    char *uri;
    GtkTargetEntry targets[] = { { "text/uri-list", 0, 0 } };

    if (g_str_has_suffix(filename, ".desktop")) {
        GDesktopAppInfo *app_info = g_desktop_app_info_new_from_filename(full_path);
        if (app_info) {
            g_free(display_name);
            display_name = g_strdup(g_app_info_get_name(G_APP_INFO(app_info)));
            if (gicon) g_object_unref(gicon);
            gicon = g_app_info_get_icon(G_APP_INFO(app_info));
            if (gicon) g_object_ref(gicon);
            g_object_unref(app_info);
        }
    }

    btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_name(btn, "desktop-item");
    gtk_widget_set_size_request(btn, ITEM_WIDTH, ITEM_HEIGHT);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(btn), box);

    image = gtk_image_new_from_gicon(gicon, GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(image), ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

    label = gtk_label_new(display_name);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(label), 3);
    gtk_label_set_width_chars(GTK_LABEL(label), 16);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 16);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    f = g_file_new_for_path(full_path);
    uri = g_file_get_uri(f);
    g_object_set_data_full(G_OBJECT(btn), "uri", g_strdup(uri), g_free);

    gtk_drag_source_set(btn, GDK_BUTTON1_MASK, targets, 1, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(btn, "drag-begin", G_CALLBACK(on_drag_begin), NULL);
    g_signal_connect(btn, "drag-data-get", G_CALLBACK(on_drag_data_get), NULL);

    if (is_dir) {
        gtk_drag_dest_set(btn, GTK_DEST_DEFAULT_ALL, targets, 1, GDK_ACTION_MOVE | GDK_ACTION_COPY);
        g_signal_connect(btn, "drag-data-received", G_CALLBACK(on_folder_drag_data_received), g_strdup(uri));
    }

    gtk_widget_add_events(btn, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect_data(btn, "button-press-event", G_CALLBACK(on_item_button_press),
                          g_strdup(uri), free_signal_data, 0);

    g_object_unref(f);
    g_free(display_name);
    if (gicon) g_object_unref(gicon);
    g_free(uri);

    gtk_widget_show_all(btn);
    return btn;
}

static gint compare_display_names(GFileInfo *ia, GFileInfo *ib) {
    return g_ascii_strcasecmp(g_file_info_get_display_name(ia),
                              g_file_info_get_display_name(ib));
}

gint sort_file_info(gconstpointer a, gconstpointer b) {
    GFileInfo *ia = (GFileInfo *)a;
    GFileInfo *ib = (GFileInfo *)b;
    DesktopSortMode mode = get_current_sort_mode();

    if (mode == SORT_TYPE) {
        const char *type_a = g_file_info_get_content_type(ia);
        const char *type_b = g_file_info_get_content_type(ib);
        gint result = g_ascii_strcasecmp(type_a ? type_a : "", type_b ? type_b : "");
        if (result != 0) return result;
    } else if (mode == SORT_DATE_MODIFIED) {
        guint64 time_a = g_file_info_get_attribute_uint64(ia, G_FILE_ATTRIBUTE_TIME_MODIFIED);
        guint64 time_b = g_file_info_get_attribute_uint64(ib, G_FILE_ATTRIBUTE_TIME_MODIFIED);
        if (time_a != time_b) return (time_a < time_b) ? 1 : -1;
    } else if (mode == SORT_SIZE) {
        goffset size_a = g_file_info_get_size(ia);
        goffset size_b = g_file_info_get_size(ib);
        if (size_a != size_b) return (size_a < size_b) ? 1 : -1;
    }

    return compare_display_names(ia, ib);
}

void on_sort_mode_selected(GtkWidget *item, gpointer data) {
    (void)item;
    set_current_sort_mode(GPOINTER_TO_INT(data));
    refresh_icons();
}

void refresh_icons(void) {
    GList *children;
    GList *iter;
    char *desktop_path;
    GFile *dir;
    GFileEnumerator *enumerator;

    children = gtk_container_get_children(GTK_CONTAINER(icon_layout));
    deselect_all();

    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        GtkWidget *child = GTK_WIDGET(iter->data);
        const gchar *name = gtk_widget_get_name(child);
        if (g_strcmp0(name, "vaxp-widget") != 0) {
            gtk_widget_destroy(child);
        } else {
            apply_widget_visibility(child);
        }
    }
    g_list_free(children);

    desktop_path = get_current_desktop_path();
    if (!desktop_path) return;

    dir = g_file_new_for_path(desktop_path);
    enumerator = g_file_enumerate_children(dir, "standard::*,time::modified",
                                           G_FILE_QUERY_INFO_NONE, NULL, NULL);

    if (enumerator) {
        GList *file_list = NULL;
        GFileInfo *info;
        GList *pending_items = NULL;
        GList *occupied_rects = NULL;
        DesktopSortMode sort_mode;
        gboolean use_manual_positions;

        while ((info = g_file_enumerator_next_file(enumerator, NULL, NULL))) {
            const char *fname = g_file_info_get_name(info);
            if (fname[0] != '.') file_list = g_list_prepend(file_list, info);
            else g_object_unref(info);
        }
        g_object_unref(enumerator);

        file_list = g_list_sort(file_list, sort_file_info);
        sort_mode = get_current_sort_mode();
        use_manual_positions = (sort_mode == SORT_MANUAL);

        for (GList *l = file_list; l != NULL; l = l->next) {
            int x;
            int y;
            const char *fname;
            char *full_path;
            GtkWidget *item;

            info = (GFileInfo *)l->data;
            fname = g_file_info_get_name(info);
            full_path = g_strdup_printf("%s/%s", desktop_path, fname);
            item = create_desktop_item(info, full_path);
            g_free(full_path);

            if (use_manual_positions && get_item_position(fname, &x, &y)) {
                GdkRectangle *r;
                gtk_layout_put(GTK_LAYOUT(icon_layout), item, x, y);
                r = g_new(GdkRectangle, 1);
                r->x = x;
                r->y = y;
                r->width = ITEM_WIDTH;
                r->height = ITEM_HEIGHT;
                occupied_rects = g_list_prepend(occupied_rects, r);
            } else {
                pending_items = g_list_append(pending_items, item);
            }
        }

        {
            int grid_x = GRID_X;
            int grid_y = GRID_Y;

            for (GList *l = pending_items; l != NULL; l = l->next) {
                GtkWidget *item = GTK_WIDGET(l->data);

                while (TRUE) {
                    gboolean collision = FALSE;
                    GdkRectangle candidate = { grid_x, grid_y, ITEM_WIDTH, ITEM_HEIGHT };

                    for (GList *r_node = occupied_rects; r_node != NULL; r_node = r_node->next) {
                        GdkRectangle *occ = (GdkRectangle *)r_node->data;
                        if (gdk_rectangle_intersect(&candidate, occ, NULL)) {
                            collision = TRUE;
                            break;
                        }
                    }

                    if (!collision) break;

                    grid_y += ITEM_HEIGHT + 10;
                    if (grid_y > screen_h - ITEM_HEIGHT) {
                        grid_y = GRID_Y;
                        grid_x += ITEM_WIDTH + 10;
                    }
                }

                gtk_layout_put(GTK_LAYOUT(icon_layout), item, grid_x, grid_y);

                {
                    GdkRectangle *r = g_new(GdkRectangle, 1);
                    r->x = grid_x;
                    r->y = grid_y;
                    r->width = ITEM_WIDTH;
                    r->height = ITEM_HEIGHT;
                    occupied_rects = g_list_prepend(occupied_rects, r);
                }
            }
        }

        g_list_free(pending_items);
        g_list_free_full(file_list, g_object_unref);
        g_list_free_full(occupied_rects, g_free);
    }

    g_object_unref(dir);
    g_free(desktop_path);
}
