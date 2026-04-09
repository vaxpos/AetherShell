/*
 * menu.h
 * Header file for context menus
 */

#ifndef MENU_H
#define MENU_H

#include <gtk/gtk.h>

/* Helper Functions */
GtkWidget *build_templates_submenu(void);
void style_context_menu(GtkWidget *menu);

/* Item Menu Callbacks */
void on_item_rename(GtkWidget *menuitem, gpointer data);
void on_item_cut(GtkWidget *menuitem, gpointer data);
void on_item_copy(GtkWidget *menuitem, gpointer data);
void on_item_open(GtkWidget *menuitem, gpointer data);
void on_item_delete(GtkWidget *menuitem, gpointer data);
gboolean on_item_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);

/* Background Menu Callbacks */
void on_bg_paste(GtkWidget *item, gpointer data);
void on_create_folder(GtkWidget *item, gpointer data);
void on_open_terminal(GtkWidget *item, gpointer data);
void on_refresh_clicked(GtkWidget *item, gpointer data);
void on_change_wallpaper(GtkWidget *item, gpointer data);

#endif /* MENU_H */
