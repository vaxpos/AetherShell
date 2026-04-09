/* vaxp-widget-api.h */
#ifndef vaxp_WIDGET_API_H
#define vaxp_WIDGET_API_H

#include <gtk/gtk.h>

/* API exposed from the Manager to the Widget */
typedef struct {
    GtkWidget *layout_container;
    void (*save_position)(const char *widget_name, int x, int y);
} vaxpDesktopAPI;

/* API struct returned by every valid vaxp widget (.so) */
typedef struct {
    const char *name;
    const char *description;
    const char *author;
    
    /* Initialization hook: 
     * The widget allocates its GTK UI, sets up its own internal update
     * timers (via g_timeout_add), connects mouse motion for dragging
     * via layout_container, and returns the root GtkWidget*.
     */
    GtkWidget* (*create_widget)(vaxpDesktopAPI *desktop_api);
    
    /* Optional: Called by the Desktop Manager to update the widget's background color. */
    void (*update_theme)(const char *bg_color, double opacity);
} vaxpWidgetAPI;

/* The expected factory symbol used dynamically by the loader. */
/* extern vaxpWidgetAPI vaxp_widget_init(void); */

#endif
