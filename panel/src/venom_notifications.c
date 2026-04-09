#include "venom_notifications.h"
#include <gio/gio.h>

static GDBusProxy *m_proxy = NULL;
static guint m_signal_id_history = 0;
static guint m_signal_id_dnd = 0;

static NotificationsUpdatedCallback m_history_cb = NULL;
static DndChangedCallback m_dnd_cb = NULL;
static gpointer m_callback_data = NULL;

static void free_notification_data(gpointer data) {
    NotificationData *n = (NotificationData *)data;
    g_free(n->app_name);
    g_free(n->icon_path);
    g_free(n->summary);
    g_free(n->body);
    g_free(n);
}

static void fetch_history(void) {
    if (!m_proxy || !m_history_cb) return;

    GError *error = NULL;
    GVariant *res = g_dbus_proxy_call_sync(m_proxy,
                                           "GetHistory",
                                           NULL,
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL,
                                           &error);
    if (error) {
        g_printerr("Failed to fetch history: %s\n", error->message);
        g_error_free(error);
        return;
    }

    if (res) {
        GList *history_list = NULL;
        GVariantIter *iter;
        GVariant *child = g_variant_get_child_value(res, 0);
        g_variant_get(child, "a(ussss)", &iter);
        
        guint32 id;
        gchar *app, *icon, *summary, *body;
        
        while (g_variant_iter_next(iter, "(ussss)", &id, &app, &icon, &summary, &body)) {
            NotificationData *n = g_new0(NotificationData, 1);
            n->id = id;
            n->app_name = g_strdup(app);
            n->icon_path = g_strdup(icon);
            n->summary = g_strdup(summary);
            n->body = g_strdup(body);
            history_list = g_list_append(history_list, n);
            
            g_free(app);
            g_free(icon);
            g_free(summary);
            g_free(body);
        }
        g_variant_iter_free(iter);
        g_variant_unref(child);
        g_variant_unref(res);

        m_history_cb(history_list, m_callback_data);
        
        g_list_free_full(history_list, free_notification_data);
    }
}

static void fetch_dnd(void) {
    if (!m_proxy || !m_dnd_cb) return;

    GError *error = NULL;
    GVariant *res = g_dbus_proxy_call_sync(m_proxy,
                                           "GetDoNotDisturb",
                                           NULL,
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL,
                                           &error);
    if (!error && res) {
        gboolean enabled;
        g_variant_get(res, "(b)", &enabled);
        m_dnd_cb(enabled, m_callback_data);
        g_variant_unref(res);
    } else if (error) {
        g_error_free(error);
    }
}

static void on_signal(GDBusProxy *proxy, gchar *sender_name, gchar *signal_name,
                      GVariant *parameters, gpointer user_data) {
    if (g_strcmp0(signal_name, "HistoryUpdated") == 0) {
        fetch_history();
    } else if (g_strcmp0(signal_name, "DoNotDisturbChanged") == 0) {
        if (m_dnd_cb) {
            gboolean enabled;
            g_variant_get(parameters, "(b)", &enabled);
            m_dnd_cb(enabled, m_callback_data);
        }
    }
}

static void on_proxy_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    m_proxy = g_dbus_proxy_new_for_bus_finish(res, &error);
    if (!m_proxy) {
        g_printerr("Error connecting to Venom notifications: %s\n", error->message);
        g_error_free(error);
        return;
    }

    g_signal_connect(m_proxy, "g-signal", G_CALLBACK(on_signal), NULL);
    
    // Initial fetch
    fetch_dnd();
    fetch_history();
}

void venom_notifications_init(NotificationsUpdatedCallback history_cb, DndChangedCallback dnd_cb, gpointer user_data) {
    m_history_cb = history_cb;
    m_dnd_cb = dnd_cb;
    m_callback_data = user_data;

    g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION,
                             G_DBUS_PROXY_FLAGS_NONE,
                             NULL,
                             "org.freedesktop.Notifications",
                             "/org/freedesktop/Notifications",
                             "org.venom.NotificationHistory",
                             NULL,
                             on_proxy_ready,
                             NULL);
}

void venom_notifications_set_dnd(gboolean enabled) {
    if (!m_proxy) return;
    g_dbus_proxy_call(m_proxy,
                      "SetDoNotDisturb",
                      g_variant_new("(b)", enabled),
                      G_DBUS_CALL_FLAGS_NONE,
                      -1,
                      NULL,
                      NULL,
                      NULL);
}

void venom_notifications_clear_history(void) {
    if (!m_proxy) return;
    g_dbus_proxy_call(m_proxy,
                      "ClearHistory",
                      NULL,
                      G_DBUS_CALL_FLAGS_NONE,
                      -1,
                      NULL,
                      NULL,
                      NULL);
}
