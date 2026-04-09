#ifndef BATTERY_INDICATOR_H
#define BATTERY_INDICATOR_H

#include <gtk/gtk.h>

// Initialize the battery indicator and return a widget containing the icon and percentage label
GtkWidget* get_battery_widget(void);

#endif // BATTERY_INDICATOR_H
