#include "logic/app_manager.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static void child_setup(gpointer user_data) {
    (void)user_data;
    setsid();
}

/* Static cache */
static GList *cached_apps = NULL;
static gboolean cache_initialized = FALSE;

/* Programmatic Fallback */
static GdkPixbuf *create_fallback_pixbuf(void) {
    GdkPixbuf *pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 48, 48);
    if (pix) gdk_pixbuf_fill(pix, 0x777777FF); /* Neutral Grey */
    return pix;
}

/* Helper to load icon */
static GdkPixbuf *load_app_icon(GAppInfo *info) {
    GIcon *icon = g_app_info_get_icon(info);
    GError *error = NULL;
    GdkPixbuf *pix = NULL;
    
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    
    if (icon) {
        GtkIconInfo *icon_info = gtk_icon_theme_lookup_by_gicon(theme, icon, 64, GTK_ICON_LOOKUP_FORCE_SIZE);
        if (icon_info) {
            pix = gtk_icon_info_load_icon(icon_info, &error);
            g_object_unref(icon_info);
            
            if (!pix) {
                if (error) g_error_free(error);
                error = NULL;
            }
        }
    }
    
    /* Fallback 1: Generic Icon (Only if it exists) */
    if (!pix) {
        if (gtk_icon_theme_has_icon(theme, "application-x-executable")) {
             pix = gtk_icon_theme_load_icon(theme, "application-x-executable", 64, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
        }
    }
    
    /* Fallback 2: Generated Pixbuf (Guaranteed) */
    if (!pix) {
        pix = create_fallback_pixbuf();
    }
    
    return pix;
}

GList *app_mgr_scan_apps(void) {
    if (cache_initialized) {
        GList *copy = NULL;
        for (GList *l = cached_apps; l != NULL; l = l->next) {
            AppInfo *src = (AppInfo *)l->data;
            AppInfo *dst = g_malloc0(sizeof(AppInfo));
            dst->name = g_strdup(src->name);
            dst->icon = g_strdup(src->icon);
            dst->desktop_file_path = g_strdup(src->desktop_file_path);
            if (src->pixbuf) dst->pixbuf = g_object_ref(src->pixbuf);
            copy = g_list_append(copy, dst);
        }
        return copy;
    }

    GList *all_apps = g_app_info_get_all();
    for (GList *l = all_apps; l != NULL; l = l->next) {
        GAppInfo *app_info = (GAppInfo *)l->data;
        
        if (!g_app_info_should_show(app_info)) continue;
        
        AppInfo *info = g_malloc0(sizeof(AppInfo));
        info->name = g_strdup(g_app_info_get_name(app_info));
        
        /* Icon String (fallback) */
        GIcon *gicon = g_app_info_get_icon(app_info);
        if (gicon) {
            gchar *str = g_icon_to_string(gicon);
            info->icon = str; 
        }
        
        /* Desktop File */
        if (G_IS_DESKTOP_APP_INFO(app_info)) {
            const char *fname = g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(app_info));
            info->desktop_file_path = g_strdup(fname);
        }
        
        /* Pre-load Pixbuf */
        info->pixbuf = load_app_icon(app_info);
        
        cached_apps = g_list_append(cached_apps, info);
    }
    
    g_list_free_full(all_apps, g_object_unref);
    cache_initialized = TRUE;
    
    /* Return copy of newly created cache */
    return app_mgr_scan_apps();
}

void app_mgr_free_list(GList *apps) {
    for (GList *l = apps; l != NULL; l = l->next) {
        AppInfo *info = (AppInfo *)l->data;
        g_free(info->name);
        g_free(info->icon);
        g_free(info->desktop_file_path);
        if (info->pixbuf) g_object_unref(info->pixbuf);
        g_free(info);
    }
    g_list_free(apps);
}

gboolean app_mgr_launch_detached(const char *cmd_line, GError **error) {
    if (!cmd_line) return FALSE;
    
    int argc;
    char **argv;
    if (g_shell_parse_argv(cmd_line, &argc, &argv, error)) {
        gboolean success = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, child_setup, NULL, NULL, error);
        g_strfreev(argv);
        return success;
    }
    return FALSE;
}

gboolean app_mgr_launch(const char *desktop_file_path, GError **error) {
    GDesktopAppInfo *app_info = g_desktop_app_info_new_from_filename(desktop_file_path);
    if (!app_info) return FALSE;
    
    GdkAppLaunchContext *context = gdk_display_get_app_launch_context(gdk_display_get_default());
    
    /* Logic: Try detached if command line available */
    const gchar *raw_cmd = g_app_info_get_commandline(G_APP_INFO(app_info));
    gboolean success = FALSE;

    if (raw_cmd) {
        gchar *cmd_line = g_strdup(raw_cmd);
        /* Strip codes */
        const char *codes[] = {"%f", "%u", "%F", "%U", "%i", "%c", "%k", NULL};
        for (int i = 0; codes[i] != NULL; i++) {
            gchar *found;
            while ((found = strstr(cmd_line, codes[i])) != NULL) {
                found[0] = ' ';
                found[1] = ' ';
            }
        }
        
        success = app_mgr_launch_detached(cmd_line, error);
        if (!success && *error) {
             /* Reset error for fallback? */
             g_clear_error(error);
        }
        g_free(cmd_line);
    }
    
    if (!success) {
        /* Fallback */
        success = g_app_info_launch(G_APP_INFO(app_info), NULL, G_APP_LAUNCH_CONTEXT(context), error);
    }
    
    g_object_unref(context);
    g_object_unref(app_info);
    
    return success;
}

