/*
 * selection.h
 * Header file for selection and rubber band
 */

#ifndef SELECTION_H
#define SELECTION_H

#include <gtk/gtk.h>

/* Selection State Variables */
extern double start_x;
extern double start_y;
extern double current_x;
extern double current_y;
extern gboolean is_selecting;
extern GList *selected_items;

/* Function Declarations */
gboolean is_selected(GtkWidget *item);
void select_item(GtkWidget *item);
void deselect_item(GtkWidget *item);
void deselect_all(void);

/* Drawing and Event Functions */
gboolean on_layout_draw_fg(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean on_bg_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
gboolean on_bg_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data);
gboolean on_bg_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data);

#endif /* SELECTION_H */
