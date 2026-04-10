#ifndef SIDEBAR_POPUP_H
#define SIDEBAR_POPUP_H

#include <gtk/gtk.h>

GtkWidget *init_sidebar_popup(void);
void sidebar_popup_set_relative_to(GtkWidget *popup, GtkWidget *relative_to);
void sidebar_popup_toggle(GtkWidget *popup, GtkWidget *relative_to);

#endif
