#ifndef PAGER_H
#define PAGER_H

#include <gtk/gtk.h>
void pager_init(void);

/* Create the Pager UI widget */
GtkWidget *pager_create_widget(void);

/* Callback for when a workspace is clicked */
typedef void (*PagerClickCallback)(int desktop_idx, gpointer user_data);
void pager_set_click_callback(PagerClickCallback callback, gpointer user_data);

/* Update pager state */
void pager_update(void);

#endif
