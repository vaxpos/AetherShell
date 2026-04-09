#ifndef UTILS_H
#define UTILS_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

/* Opens a given path (e.g. "trash:///" or a file path) using the preferred file manager
 * Precedence: vafile -> nautilus -> xdg-open 
 */
void open_with_preferred_file_manager(const gchar *path);
GdkPixbuf *create_rounded_icon_pixbuf(GdkPixbuf *source, gint size, gdouble radius);
const gchar *dock_get_resource_base_dir(void);
gchar *dock_build_resource_path(const gchar *filename);

#endif /* UTILS_H */
