#ifndef VENOM_NOTIFICATIONS_H
#define VENOM_NOTIFICATIONS_H

#include <glib.h>

// Struct for history items
typedef struct {
    guint32 id;
    char *app_name;
    char *icon_path;
    char *summary;
    char *body;
} NotificationData;

typedef void (*NotificationsUpdatedCallback)(GList *history, gpointer user_data);
typedef void (*DndChangedCallback)(gboolean enabled, gpointer user_data);

void venom_notifications_init(NotificationsUpdatedCallback history_cb,
                              DndChangedCallback dnd_cb,
                              gpointer user_data);

void venom_notifications_set_dnd(gboolean enabled);
void venom_notifications_clear_history(void);

#endif // VENOM_NOTIFICATIONS_H
