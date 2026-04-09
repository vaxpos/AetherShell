#ifndef APP_MENU_H
#define APP_MENU_H

#include <gtk/gtk.h>

// Initializes the app menu popover
GtkWidget* init_app_menu(void);
void app_menu_set_relative_to(GtkWidget *menu, GtkWidget *relative_to);

#endif // APP_MENU_H
