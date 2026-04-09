#pragma once

#include <gtk/gtk.h>
#include <glib.h>

G_BEGIN_DECLS

#define VENOM_TYPE_LAUNCHER_WINDOW (venom_launcher_window_get_type ())
G_DECLARE_FINAL_TYPE (VenomLauncherWindow, venom_launcher_window,
                      VENOM, LAUNCHER_WINDOW, GtkApplicationWindow)

/**
 * VenomLauncherWindow - Fullscreen, translucent launcher window.
 * Owns the app list, search bar, and icon grid.
 */
GtkWidget *venom_launcher_window_new (GtkApplication *app);
void       venom_launcher_window_show_launcher (VenomLauncherWindow *win);

G_END_DECLS
