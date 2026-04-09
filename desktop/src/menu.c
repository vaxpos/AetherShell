/*
 * menu.c
 * Context menus and file/menu actions.
 */

#include "menu.h"
#include "desktop_config.h"
#include "filesystem.h"
#include "icons.h"
#include "selection.h"
#include "wallpaper.h"
#include "widgets_manager.h"
#include <glib/gstdio.h>
#include <string.h>

static void free_signal_data(gpointer data, GClosure *closure) {
    (void)closure;
    g_free(data);
}

void style_context_menu(GtkWidget *menu) {
    GdkScreen *screen;
    GdkVisual *visual;
    GtkStyleContext *context;

    if (!menu) {
        return;
    }

    gtk_widget_set_name(menu, "desktop-context-menu");
    gtk_widget_set_app_paintable(menu, TRUE);

    screen = gtk_widget_get_screen(menu);
    if (screen) {
        visual = gdk_screen_get_rgba_visual(screen);
        if (visual && gdk_screen_is_composited(screen)) {
            gtk_widget_set_visual(menu, visual);
        }
    }

    context = gtk_widget_get_style_context(menu);
    gtk_style_context_add_class(context, "desktop-context-menu");
}

static void style_blur_dialog(GtkWidget *dialog) {
    GdkScreen *screen;
    GdkVisual *visual;
    GtkStyleContext *context;
    GtkWidget *content;

    if (!dialog) {
        return;
    }

    gtk_widget_set_name(dialog, "desktop-blur-dialog");
    gtk_widget_set_app_paintable(dialog, TRUE);

    screen = gtk_widget_get_screen(dialog);
    if (screen) {
        visual = gdk_screen_get_rgba_visual(screen);
        if (visual && gdk_screen_is_composited(screen)) {
            gtk_widget_set_visual(dialog, visual);
        }
    }

    context = gtk_widget_get_style_context(dialog);
    gtk_style_context_add_class(context, "desktop-blur-dialog");

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    if (content) {
        gtk_widget_set_name(content, "desktop-blur-dialog-content");
        gtk_widget_set_app_paintable(content, TRUE);
        gtk_container_set_border_width(GTK_CONTAINER(content), 10);
    }
}

void on_item_rename(GtkWidget *menuitem, gpointer data) {
    char *uri = (char *)data;
    GFile *file = g_file_new_for_uri(uri);
    char *name = g_file_get_basename(file);
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *entry;

    (void)menuitem;

    dialog = gtk_dialog_new_with_buttons("Rename", GTK_WINDOW(main_window),
                                         GTK_DIALOG_MODAL,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Rename", GTK_RESPONSE_ACCEPT,
                                         NULL);
    style_blur_dialog(dialog);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), name);
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *new_name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (strlen(new_name) > 0) {
            GFile *new_file = g_file_set_display_name(file, new_name, NULL, NULL);
            if (new_file) {
                g_object_unref(new_file);
                refresh_icons();
            }
        }
    }

    gtk_widget_destroy(dialog);
    g_free(name);
    g_object_unref(file);
}

void on_item_cut(GtkWidget *menuitem, gpointer data) {
    (void)menuitem;
    (void)data;
    copy_selection_to_clipboard(TRUE);
}

void on_item_copy(GtkWidget *menuitem, gpointer data) {
    (void)menuitem;
    (void)data;
    copy_selection_to_clipboard(FALSE);
}

void on_item_open(GtkWidget *menuitem, gpointer data) {
    (void)menuitem;
    open_file_uri((char *)data);
}

void on_item_delete(GtkWidget *menuitem, gpointer data) {
    GList *uris_to_delete = NULL;

    (void)menuitem;
    (void)data;

    for (GList *l = selected_items; l != NULL; l = l->next) {
        char *uri = (char *)g_object_get_data(G_OBJECT(l->data), "uri");
        if (uri) uris_to_delete = g_list_prepend(uris_to_delete, g_strdup(uri));
    }

    for (GList *l = uris_to_delete; l != NULL; l = l->next) {
        delete_file((char *)l->data);
        g_free(l->data);
    }
    g_list_free(uris_to_delete);

    refresh_icons();
}

gboolean on_item_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    char *uri = (char *)data;

    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        if (!is_selected(widget)) {
            if (!(event->state & GDK_CONTROL_MASK)) deselect_all();
            select_item(widget);
        }

        drag_start_x_root = event->x_root;
        drag_start_y_root = event->y_root;

        if (drag_initial_positions) {
            GHashTableIter iter;
            gpointer key;
            gpointer value;

            g_hash_table_iter_init(&iter, drag_initial_positions);
            while (g_hash_table_iter_next(&iter, &key, &value)) g_free(value);
            g_hash_table_destroy(drag_initial_positions);
        }
        drag_initial_positions = g_hash_table_new(g_direct_hash, g_direct_equal);

        for (GList *l = selected_items; l != NULL; l = l->next) {
            GtkWidget *item = GTK_WIDGET(l->data);
            int x;
            int y;
            int *pos = g_new(int, 2);
            gtk_container_child_get(GTK_CONTAINER(icon_layout), item, "x", &x, "y", &y, NULL);
            pos[0] = x;
            pos[1] = y;
            g_hash_table_insert(drag_initial_positions, item, pos);
        }

        return FALSE;
    }

    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkWidget *menu;

        if (!is_selected(widget)) {
            deselect_all();
            select_item(widget);
        }

        menu = gtk_menu_new();
        style_context_menu(menu);

        if (g_list_length(selected_items) > 1) {
            GtkWidget *i_cut = gtk_menu_item_new_with_label("Cut");
            GtkWidget *i_copy = gtk_menu_item_new_with_label("Copy");
            GtkWidget *i_del = gtk_menu_item_new_with_label("Move to Trash");

            g_signal_connect(i_cut, "activate", G_CALLBACK(on_item_cut), NULL);
            g_signal_connect(i_copy, "activate", G_CALLBACK(on_item_copy), NULL);
            g_signal_connect(i_del, "activate", G_CALLBACK(on_item_delete), NULL);

            gtk_menu_shell_append(GTK_MENU_SHELL(menu), i_cut);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), i_copy);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), i_del);
        } else {
            GtkWidget *i_open = gtk_menu_item_new_with_label("Open");
            GtkWidget *i_cut = gtk_menu_item_new_with_label("Cut");
            GtkWidget *i_copy = gtk_menu_item_new_with_label("Copy");
            GtkWidget *i_rename = gtk_menu_item_new_with_label("Rename");
            GtkWidget *i_del = gtk_menu_item_new_with_label("Move to Trash");

            g_signal_connect(i_open, "activate", G_CALLBACK(on_item_open), uri);
            g_signal_connect(i_cut, "activate", G_CALLBACK(on_item_cut), NULL);
            g_signal_connect(i_copy, "activate", G_CALLBACK(on_item_copy), NULL);
            g_signal_connect(i_rename, "activate", G_CALLBACK(on_item_rename), uri);
            g_signal_connect(i_del, "activate", G_CALLBACK(on_item_delete), NULL);

            gtk_menu_shell_append(GTK_MENU_SHELL(menu), i_open);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), i_cut);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), i_copy);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), i_rename);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), i_del);
        }

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }

    if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
        open_file_uri(uri);
        return TRUE;
    }

    return FALSE;
}

void on_bg_paste(GtkWidget *item, gpointer data) {
    (void)item;
    (void)data;
    paste_from_clipboard();
}

void on_change_wallpaper(GtkWidget *item, gpointer data) {
    (void)item;
    show_wallpaper_picker(GTK_WIDGET(data));
}

void on_create_folder(GtkWidget *item, gpointer data) {
    char *desktop_path = get_current_desktop_path();
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *entry;

    (void)item;
    (void)data;

    if (!desktop_path) return;

    dialog = gtk_dialog_new_with_buttons("Create Folder", GTK_WINDOW(main_window),
                                         GTK_DIALOG_MODAL,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Create", GTK_RESPONSE_ACCEPT,
                                         NULL);
    style_blur_dialog(dialog);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (strlen(name) > 0) {
            char *path = g_strdup_printf("%s/%s", desktop_path, name);
            g_mkdir(path, 0755);
            g_free(path);
            refresh_icons();
        }
    }

    gtk_widget_destroy(dialog);
    g_free(desktop_path);
}

void on_open_terminal(GtkWidget *item, gpointer data) {
    char *desktop_path = get_current_desktop_path();

    (void)item;
    (void)data;

    if (!desktop_path) return;

    {
        char *cmd = g_strdup_printf("exo-open --launch TerminalEmulator --working-directory=%s", desktop_path);
        g_spawn_command_line_async(cmd, NULL);
        g_free(cmd);
    }

    g_free(desktop_path);
}

void on_refresh_clicked(GtkWidget *item, gpointer data) {
    (void)item;
    (void)data;
    refresh_icons();
}

static void on_create_document(GtkWidget *menuitem, gpointer data) {
    const char *template_path = (const char *)data;
    char *basename;
    char *desktop;
    char *dest_path;
    int counter = 1;
    GFile *src;
    GFile *dest;
    GError *err = NULL;

    (void)menuitem;

    if (!template_path) return;

    basename = g_path_get_basename(template_path);
    desktop = get_current_desktop_path();
    if (!desktop) {
        g_free(basename);
        return;
    }

    dest_path = g_strdup_printf("%s/%s", desktop, basename);
    while (g_file_test(dest_path, G_FILE_TEST_EXISTS)) {
        char *dot;
        g_free(dest_path);
        dot = strrchr(basename, '.');
        if (dot) {
            char *name_part = g_strndup(basename, dot - basename);
            dest_path = g_strdup_printf("%s/%s (%d)%s", desktop, name_part, counter++, dot);
            g_free(name_part);
        } else {
            dest_path = g_strdup_printf("%s/%s (%d)", desktop, basename, counter++);
        }
    }

    src = g_file_new_for_path(template_path);
    dest = g_file_new_for_path(dest_path);
    g_file_copy(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
    if (err) {
        g_warning("[Template] Copy failed: %s", err->message);
        g_error_free(err);
    } else {
        refresh_icons();
    }

    g_object_unref(src);
    g_object_unref(dest);
    g_free(dest_path);
    g_free(desktop);
    g_free(basename);
}

GtkWidget *build_templates_submenu(void) {
    char *templates_dir = g_strdup_printf("%s/Templates", g_get_home_dir());
    GtkWidget *submenu = gtk_menu_new();
    GDir *dir = g_dir_open(templates_dir, 0, NULL);

    style_context_menu(submenu);

    if (dir) {
        const char *fname;
        int count = 0;

        while ((fname = g_dir_read_name(dir))) {
            char *full_path;
            GtkWidget *item;

            if (fname[0] == '.') continue;

            full_path = g_strdup_printf("%s/%s", templates_dir, fname);
            item = gtk_menu_item_new_with_label(fname);
            g_signal_connect_data(item, "activate",
                                  G_CALLBACK(on_create_document),
                                  g_strdup(full_path),
                                  free_signal_data, 0);
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
            g_free(full_path);
            count++;
        }
        g_dir_close(dir);

        if (count == 0) {
            GtkWidget *empty = gtk_menu_item_new_with_label("(No templates in ~/Templates)");
            gtk_widget_set_sensitive(empty, FALSE);
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), empty);
        }
    } else {
        GtkWidget *empty = gtk_menu_item_new_with_label("(~/Templates not found)");
        gtk_widget_set_sensitive(empty, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), empty);
    }

    g_free(templates_dir);
    return submenu;
}
