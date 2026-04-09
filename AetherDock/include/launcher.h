#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <gtk/gtk.h>

/* Global variables exposed for panel management */
extern GtkWidget *launcher_button;
extern GtkWidget *launcher_window;

/* Public API */
void create_launcher_button(GtkWidget *box);
void on_launcher_clicked(GtkWidget *widget, gpointer data);
void on_launcher_app_clicked(GtkWidget *widget, gpointer data);
void launcher_start_standalone(void);
void launcher_toggle_visibility(void);

#endif /* LAUNCHER_H */
