#ifndef PAGER_SERVICE_H
#define PAGER_SERVICE_H

#include <X11/Xlib.h>
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

void pager_svc_init(Display *dpy, Window root);
int pager_svc_get_current_desktop(void);
int pager_svc_get_num_desktops(void);
void pager_svc_set_desktop(int index);

/* Get all windows on a specific desktop */
/* Returns: GList of Window IDs (GINT_TO_POINTER) */
GList *pager_svc_get_windows(int desktop_index);

/* Capture window pixmap (GPU) */
Pixmap pager_svc_get_pixmap(Window win);
/* Capture snapshot (CPU Fallback) */
GdkPixbuf *pager_svc_get_snapshot_pixbuf(Window win, int width, int height);
void pager_svc_clear_cache(void);

/* Get window geometry */
gboolean pager_svc_get_window_geometry(Window win, int *x, int *y, int *width, int *height);

#endif
