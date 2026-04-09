/*
 * widgets_manager.h
 * Dynamic widget loading and widget-related UI.
 */

#ifndef WIDGETS_MANAGER_H
#define WIDGETS_MANAGER_H

#include <gtk/gtk.h>

void load_all_widgets(GtkWidget *layout);
void reload_widgets(void);
void apply_widget_visibility(GtkWidget *root);

void on_mode_normal(GtkWidget *item, gpointer data);
void on_mode_work(GtkWidget *item, gpointer data);
void on_mode_widgets(GtkWidget *item, gpointer data);
void on_edit_widgets(GtkWidget *item, gpointer data);

#endif
