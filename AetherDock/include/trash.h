#ifndef TRASH_H
#define TRASH_H

#include <gtk/gtk.h>

/* Exposed so AetherDock.c can skip it during container cleanup */
extern GtkWidget *trash_button;

/* Creates the trash icon button and packs it into the given box (pack_end) */
void create_trash_button(GtkWidget *box);

/* Update the trash icon based on whether trash folder is empty */
void trash_update_icon(void);

#endif /* TRASH_H */
