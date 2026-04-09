#pragma once

#include <gtk/gtk.h>
#include "../core/app_entry.h"

G_BEGIN_DECLS

#define VENOM_TYPE_APP_ICON (venom_app_icon_get_type ())
G_DECLARE_FINAL_TYPE (VenomAppIcon, venom_app_icon, VENOM, APP_ICON, GtkButton)

/**
 * VenomAppIcon - A single tappable app icon widget.
 * Shows a 96×96 icon image + app name label below.
 */
GtkWidget *venom_app_icon_new       (AppEntry *entry);
AppEntry  *venom_app_icon_get_entry (VenomAppIcon *icon);

G_END_DECLS
