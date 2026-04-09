#ifndef CONTROL_CENTER_UI_H
#define CONTROL_CENTER_UI_H

#include <gtk/gtk.h>

// Initializes the control center popover
GtkWidget* init_control_center(void);
void control_center_set_relative_to(GtkWidget *control_center, GtkWidget *relative_to);

#endif // CONTROL_CENTER_UI_H
