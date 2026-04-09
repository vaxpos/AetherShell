#ifndef VOLUME_INDICATOR_H
#define VOLUME_INDICATOR_H

#include <gtk/gtk.h>

/* Creates the volume indicator widget for the panel bar.
 * Returns the button widget to pack into the right_box. */
GtkWidget *create_volume_indicator_widget(void);

#endif // VOLUME_INDICATOR_H
