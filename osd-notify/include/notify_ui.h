#ifndef VENOM_GUI_NOTIFY_UI_H
#define VENOM_GUI_NOTIFY_UI_H

#include <gtk/gtk.h>
#include <gio/gio.h>

typedef struct {
    guint32 id;
    char *app_name;
    char *icon_path;
    GtkWidget *icon_img;
    GtkWidget *title_lbl;
    GtkWidget *body_lbl;
    GtkWidget *win;
    guint timeout_source;
} VenomNotification;

void notify_ui_init(void);
void notify_ui_setup_window(VenomNotification *notification,
                            const char *summary,
                            const char *body,
                            const char *icon,
                            GVariant *actions,
                            gboolean use_layer_shell,
                            void (*action_cb)(guint32 id, const char *action_key, gpointer user_data),
                            gpointer user_data);
void notify_ui_update_content(VenomNotification *notification,
                              const char *summary,
                              const char *body,
                              const char *icon);
void notify_ui_destroy(VenomNotification *notification);
void notify_ui_reposition(GList *active_notifications, gboolean use_layer_shell);

#endif
