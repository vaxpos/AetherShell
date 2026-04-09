#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <gtk/gtk.h>
#include <X11/Xlib.h>

typedef struct {
    char *wm_class;
    GList *windows;  /* List of Window IDs (GINT_TO_POINTER) */
    GdkPixbuf *icon;
    int active_index;
    char *desktop_file_path;
    gboolean is_pinned;
} WindowGroupModel;

/* Global X11 variables needed by others? Preferably not, but for now we expose what's needed */
/* We will try to encapsulate them. */

void wm_init(void);
void wm_update_window_list(void);
void wm_cleanup(void);

GHashTable *wm_get_groups(void); /* Returns wm_class -> WindowGroupModel */

/* Helper functions */
char *wm_get_window_name(Window xwindow);
char *wm_get_window_class(Window xwindow);
GdkPixbuf *wm_get_window_icon(Window xwindow);
void wm_activate_window(WindowGroupModel *group);

/* Pinning */
void wm_load_pinned_apps(GList **pinned_list_ptr);
void wm_save_pinned_apps(GList *pinned_list);

#endif
