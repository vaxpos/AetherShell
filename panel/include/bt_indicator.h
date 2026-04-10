#ifndef BT_INDICATOR_H
#define BT_INDICATOR_H

#include <gtk/gtk.h>

/**
 * create_bt_indicator_widget:
 * Creates the Bluetooth status icon for the panel bar.
 * - Shows bluetooth-disabled / bluetooth-active / bluetooth-paired icons.
 * - Left-click toggles the Bluetooth adapter on/off.
 * - Opens a small info popup showing the connected device name (if any).
 */
GtkWidget *create_bt_indicator_widget(void);

#endif /* BT_INDICATOR_H */
