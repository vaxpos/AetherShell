#ifndef NOTIFICATIONS_UI_H
#define NOTIFICATIONS_UI_H

#include <gtk/gtk.h>

GtkWidget* init_notifications_ui(void);
void notifications_ui_set_relative_to(GtkWidget *notifications_window, GtkWidget *relative_to);

#endif
