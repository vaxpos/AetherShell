#ifndef DRIVES_H
#define DRIVES_H

#include <gtk/gtk.h>

/* Exposed so AetherDock.c can skip it during container cleanup */
extern GtkWidget *drives_box;

/* Creates the drives area and packs it into the given box (pack_end).
 * Monitors mounted volumes and updates icons automatically. */
void create_drives_area(GtkWidget *box);

#endif /* DRIVES_H */
