/*
 * filesystem.c
 * File operations: copy, move, delete, clipboard
 */

#include "filesystem.h"
#include "desktop_config.h"
#include "selection.h"
#include "icons.h"
#include <gio/gdesktopappinfo.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *uri_list;
    gboolean is_cut;
} ClipboardPayload;

/* --- Recursive File Operations --- */

gboolean recursive_copy_move(GFile *src, GFile *dest, gboolean is_move, GError **error) {
    if (g_file_has_prefix(dest, src)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Cannot copy directory into itself");
        return FALSE;
    }

    GFileInfo *info = g_file_query_info(src, G_FILE_ATTRIBUTE_STANDARD_TYPE, G_FILE_QUERY_INFO_NONE, NULL, error);
    if (!info) return FALSE;
    
    GFileType type = g_file_info_get_file_type(info);
    g_object_unref(info);

    if (type == G_FILE_TYPE_DIRECTORY) {
        if (!g_file_make_directory(dest, NULL, error)) {
            /* Ignore error if directory exists */
            if (!g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
                return FALSE;
            }
            g_clear_error(error);
        }

        GFileEnumerator *enumerator = g_file_enumerate_children(src, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NONE, NULL, error);
        if (!enumerator) return FALSE;

        GFileInfo *child_info;
        while ((child_info = g_file_enumerator_next_file(enumerator, NULL, NULL))) {
            const char *name = g_file_info_get_name(child_info);
            GFile *child_src = g_file_get_child(src, name);
            GFile *child_dest = g_file_get_child(dest, name);
            
            if (!recursive_copy_move(child_src, child_dest, is_move, error)) {
                g_object_unref(child_src);
                g_object_unref(child_dest);
                g_object_unref(child_info);
                g_object_unref(enumerator);
                return FALSE;
            }
            
            g_object_unref(child_src);
            g_object_unref(child_dest);
            g_object_unref(child_info);
        }
        g_object_unref(enumerator);

        if (is_move) {
            return g_file_delete(src, NULL, error);
        }
    } else {
        if (is_move) {
            return g_file_move(src, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, error);
        } else {
            return g_file_copy(src, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, error);
        }
    }
    return TRUE;
}

/* --- File Operations --- */

void open_file_uri(const char *uri) {
    GError *err = NULL;
    GFile *file = g_file_new_for_uri(uri);
    char *path = g_file_get_path(file);

    deselect_all();
    
    if (path && g_str_has_suffix(path, ".desktop")) {
        GDesktopAppInfo *app = g_desktop_app_info_new_from_filename(path);
        if (app) {
            g_app_info_launch(G_APP_INFO(app), NULL, NULL, &err);
            g_object_unref(app);
        } else {
             g_app_info_launch_default_for_uri(uri, NULL, &err);
        }
    } else {
        g_app_info_launch_default_for_uri(uri, NULL, &err);
    }
    
    if (err) {
        g_printerr("Error launching: %s\n", err->message);
        g_error_free(err);
    }
    g_free(path);
    g_object_unref(file);
}

gboolean delete_file(const char *uri) {
    GFile *file = g_file_new_for_uri(uri);
    GError *err = NULL;
    gboolean success = TRUE;
    if (!g_file_trash(file, NULL, &err)) {
        g_warning("Trash failed: %s", err->message);
        g_error_free(err);
        success = FALSE;
    }
    g_object_unref(file);
    return success;
}

/* --- System Clipboard --- */

void clipboard_get_func(GtkClipboard *clipboard, GtkSelectionData *selection_data, guint info, gpointer user_data_or_owner) {
    ClipboardPayload *payload = (ClipboardPayload *)user_data_or_owner;

    (void)clipboard;

    if (info == 1) { /* text/uri-list */
        gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data), 8,
                               (guchar *)payload->uri_list, strlen(payload->uri_list));
    } else if (info == 2) { /* x-special/gnome-copied-files */
        char *data = g_strdup_printf("%s\n%s",
                                     payload->is_cut ? "cut" : "copy",
                                     payload->uri_list);
        gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data), 8, (guchar *)data, strlen(data));
        g_free(data);
    }
}

void clipboard_clear_func(GtkClipboard *clipboard, gpointer user_data_or_owner) {
    ClipboardPayload *payload = (ClipboardPayload *)user_data_or_owner;

    (void)clipboard;

    if (!payload) return;
    g_free(payload->uri_list);
    g_free(payload);
}

void copy_selection_to_clipboard(gboolean is_cut) {
    ClipboardPayload *payload;

    if (!selected_items) return;

    GString *uri_list = g_string_new("");
    for (GList *l = selected_items; l != NULL; l = l->next) {
        GtkWidget *item = GTK_WIDGET(l->data);
        char *uri = (char *)g_object_get_data(G_OBJECT(item), "uri");
        if (uri) {
            g_string_append_printf(uri_list, "%s\r\n", uri);
        }
    }

    if (uri_list->len == 0) {
        g_string_free(uri_list, TRUE);
        return;
    }

    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    GtkTargetEntry targets[] = {
        { "text/uri-list", 0, 1 },
        { "x-special/gnome-copied-files", 0, 2 }
    };
    
    payload = g_new0(ClipboardPayload, 1);
    payload->uri_list = g_string_free(uri_list, FALSE);
    payload->is_cut = is_cut;

    gtk_clipboard_set_with_data(clipboard, targets, 2,
                                clipboard_get_func, clipboard_clear_func, payload);
}

void on_paste_received(GtkClipboard *clipboard, GtkSelectionData *selection_data, gpointer data) {
    char *desktop_path;

    (void)clipboard;
    (void)data;

    if (!selection_data || gtk_selection_data_get_length(selection_data) <= 0) return;

    desktop_path = get_current_desktop_path();
    if (!desktop_path) return;

    gchar *content = (gchar *)gtk_selection_data_get_data(selection_data);
    gchar **lines = g_strsplit(content, "\n", -1);
    
    gboolean is_cut = FALSE;
    int start_idx = 0;

    if (lines[0] && g_strcmp0(lines[0], "cut") == 0) {
        is_cut = TRUE;
        start_idx = 1;
    } else if (lines[0] && g_strcmp0(lines[0], "copy") == 0) {
        start_idx = 1;
    }

    for (int i = start_idx; lines[i] != NULL; i++) {
        gchar *uri = g_strstrip(lines[i]);
        if (strlen(uri) == 0) continue;
        if (g_str_has_prefix(uri, "#")) continue;

        GFile *src = g_file_new_for_uri(uri);
        char *basename = g_file_get_basename(src);
        char *dest_path = g_strdup_printf("%s/%s", desktop_path, basename);
        GFile *dest = g_file_new_for_path(dest_path);
        
        GError *err = NULL;
        recursive_copy_move(src, dest, is_cut, &err);

        if (err) {
            g_warning("Paste error: %s", err->message);
            g_error_free(err);
        }
        
        g_free(basename);
        g_free(dest_path);
        g_object_unref(src);
        g_object_unref(dest);
    }
    g_strfreev(lines);
    g_free(desktop_path);
    refresh_icons();
}

void paste_from_clipboard(void) {
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (gtk_clipboard_wait_is_target_available(clipboard, gdk_atom_intern("x-special/gnome-copied-files", FALSE))) {
        gtk_clipboard_request_contents(clipboard, gdk_atom_intern("x-special/gnome-copied-files", FALSE), on_paste_received, NULL);
    } else {
        gtk_clipboard_request_contents(clipboard, gdk_atom_intern("text/uri-list", FALSE), on_paste_received, NULL);
    }
}
