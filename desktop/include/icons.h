/*
 * icons.h
 * Header file for icon management
 */

#ifndef ICONS_H
#define ICONS_H

#include <gtk/gtk.h>
#include <gio/gio.h>
#include "desktop_config.h"

/* Function Declarations */
GtkWidget* create_desktop_item(GFileInfo *info, const char *full_path);
gint sort_file_info(gconstpointer a, gconstpointer b);
void on_sort_mode_selected(GtkWidget *item, gpointer data);
void refresh_icons(void);

/* Drag and Drop Functions */
void on_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer data);
void on_drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *data, guint info, guint time, gpointer user_data);
void on_folder_drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data, guint info, guint time, gpointer user_data);
void on_bg_drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data, guint info, guint time, gpointer user_data);

/* Drag State Variables */
extern double drag_start_x_root;
extern double drag_start_y_root;
extern GHashTable *drag_initial_positions;

#endif /* ICONS_H */
