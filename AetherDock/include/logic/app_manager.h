#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>

/* Helper struct for app info */
typedef struct {
    char *name;
    char *icon;
    char *desktop_file_path;
    GdkPixbuf *pixbuf; /* Cached icon for performance */
    /* Add more fields if needed */
} AppInfo;

/* Scans /usr/share/applications and returns valid apps */
/* Returns: GList of AppInfo* */
GList *app_mgr_scan_apps(void);

/* Free the list returned by scan */
void app_mgr_free_list(GList *apps);

/* Launch an app by desktop file path */
/* Returns TRUE on success */
gboolean app_mgr_launch(const char *desktop_file_path, GError **error);

/* Launch app wrapper detached */
gboolean app_mgr_launch_detached(const char *cmd_line, GError **error);

#endif
