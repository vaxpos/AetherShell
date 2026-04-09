/*
 * filesystem.h
 * Header file for file operations
 */

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <gtk/gtk.h>
#include <gio/gio.h>

/* Function Declarations */
gboolean recursive_copy_move(GFile *src, GFile *dest, gboolean is_move, GError **error);
void open_file_uri(const char *uri);
gboolean delete_file(const char *uri);

/* Clipboard Functions */
void clipboard_get_func(GtkClipboard *clipboard, GtkSelectionData *selection_data, guint info, gpointer user_data_or_owner);
void clipboard_clear_func(GtkClipboard *clipboard, gpointer user_data_or_owner);
void copy_selection_to_clipboard(gboolean is_cut);
void on_paste_received(GtkClipboard *clipboard, GtkSelectionData *selection_data, gpointer data);
void paste_from_clipboard(void);

#endif /* FILESYSTEM_H */
