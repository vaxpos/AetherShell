#ifndef SNI_TRAY_H
#define SNI_TRAY_H

#include <gtk/gtk.h>

// Creates a StatusNotifierItem tray widget for Wayland sessions.
// The implementation exports/uses org.kde.StatusNotifierWatcher directly.
GtkWidget* create_sni_tray_widget(void);

#endif // SNI_TRAY_H
