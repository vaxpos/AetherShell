#ifndef WINDOW_BACKEND_H
#define WINDOW_BACKEND_H

#include <gtk/gtk.h>
#include <gtk-layer-shell.h>

gboolean panel_window_backend_is_wayland(void);

void panel_window_backend_init_panel(GtkWindow *window, const char *namespace_name);
void panel_window_backend_init_popup(GtkWindow *window,
                                     const char *namespace_name,
                                     GdkWindowTypeHint type_hint,
                                     GtkLayerShellKeyboardMode keyboard_mode);
void panel_window_backend_set_anchor(GtkWindow *window,
                                     GtkLayerShellEdge edge,
                                     gboolean anchor_to_edge);
void panel_window_backend_set_margin(GtkWindow *window,
                                     GtkLayerShellEdge edge,
                                     gint margin);
void panel_window_backend_auto_exclusive_zone_enable(GtkWindow *window);

#endif
