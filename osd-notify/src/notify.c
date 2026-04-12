#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gtk-layer-shell.h>
#if defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#endif
#include <time.h>
#include "notify_ui.h"

#define DEFAULT_TIMEOUT 5000
#define MAX_HISTORY 50

// --- هيكل بيانات السجل (History Item) ---
typedef struct {
    guint32 id;
    char *app_name;
    char *icon_path;
    char *summary;
    char *body;
    gint64 timestamp;
} NotificationItem;

GList *active_notifications = NULL;
GList *history_list = NULL;
guint32 id_counter = 1;
gboolean do_not_disturb = FALSE;
GDBusConnection *dbus_connection = NULL;
gboolean notify_use_layer_shell = FALSE;

// --- واجهات D-Bus ---
static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='org.freedesktop.Notifications'>"
  "    <method name='Notify'>"
  "      <arg type='s' name='app_name' direction='in'/>"
  "      <arg type='u' name='replaces_id' direction='in'/>"
  "      <arg type='s' name='app_icon' direction='in'/>"
  "      <arg type='s' name='summary' direction='in'/>"
  "      <arg type='s' name='body' direction='in'/>"
  "      <arg type='as' name='actions' direction='in'/>"
  "      <arg type='a{sv}' name='hints' direction='in'/>"
  "      <arg type='i' name='expire_timeout' direction='in'/>"
  "      <arg type='u' name='id' direction='out'/>"
  "    </method>"
  "    <method name='GetCapabilities'><arg type='as' name='caps' direction='out'/></method>"
  "    <method name='GetServerInformation'><arg type='s' name='name' direction='out'/><arg type='s' name='vendor' direction='out'/><arg type='s' name='ver' direction='out'/><arg type='s' name='spec' direction='out'/></method>"
  "    <method name='CloseNotification'><arg type='u' name='id' direction='in'/></method>"
  "    <signal name='NotificationClosed'>"
  "      <arg type='u' name='id'/>"
  "      <arg type='u' name='reason'/>"
  "    </signal>"
  "    <signal name='ActionInvoked'>"
  "      <arg type='u' name='id'/>"
  "      <arg type='s' name='action_key'/>"
  "    </signal>"
  "  </interface>"
  "  <interface name='org.venom.NotificationHistory'>"
  "    <method name='GetHistory'><arg type='a(ussss)' name='notifications' direction='out'/></method>"
  "    <method name='ClearHistory'/>"
  "    <method name='SetDoNotDisturb'><arg type='b' name='enabled' direction='in'/></method>"
  "    <method name='GetDoNotDisturb'><arg type='b' name='enabled' direction='out'/></method>"
  "    <signal name='HistoryUpdated'/>"
  "    <signal name='DoNotDisturbChanged'><arg type='b' name='enabled'/></signal>"
  "  </interface>"
  "</node>";

// --- تعريف مسبق للدوال ---
void close_notification(guint32 id, guint reason);
void emit_history_updated_signal(GDBusConnection *connection);
void add_to_history(guint32 id, const char *app, const char *icon, const char *summary, const char *body);
void clear_history();
static NotificationItem *find_history_item_by_id(guint32 id);
static VenomNotification *find_active_notification_by_id(guint32 id);
static void on_ui_action(guint32 id, const char *action_key, gpointer user_data);
static guint32 create_new_notification(const char *app_name, guint32 replaces_id, const char *summary, const char *body, const char *icon, GVariant *actions, gint timeout);
static gboolean notify_is_wayland_session(void);

// --- إدارة السجل (History Logic) ---

void add_to_history(guint32 id, const char *app, const char *icon, const char *summary, const char *body) {
    NotificationItem *item = g_new0(NotificationItem, 1);
    item->id = id;
    item->app_name = g_strdup(app);
    item->icon_path = g_strdup(icon);
    item->summary = g_strdup(summary);
    item->body = g_strdup(body);
    item->timestamp = time(NULL);

    // إضافة لأول القائمة (الأحدث أولاً)
    history_list = g_list_prepend(history_list, item);

    // تنظيف القديم إذا تجاوزنا الحد المسموح
    if (g_list_length(history_list) > MAX_HISTORY) {
        GList *last = g_list_last(history_list);
        NotificationItem *old_item = (NotificationItem *)last->data;
        g_free(old_item->app_name);
        g_free(old_item->icon_path);
        g_free(old_item->summary);
        g_free(old_item->body);
        g_free(old_item);
        history_list = g_list_delete_link(history_list, last);
    }
}

void clear_history() {
    GList *l;
    for (l = history_list; l != NULL; l = l->next) {
        NotificationItem *item = (NotificationItem *)l->data;
        g_free(item->app_name);
        g_free(item->icon_path);
        g_free(item->summary);
        g_free(item->body);
        g_free(item);
    }
    g_list_free(history_list);
    history_list = NULL;
}

static NotificationItem *find_history_item_by_id(guint32 id) {
    for (GList *l = history_list; l != NULL; l = l->next) {
        NotificationItem *item = (NotificationItem *)l->data;
        if (item->id == id) {
            return item;
        }
    }
    return NULL;
}

static VenomNotification *find_active_notification_by_id(guint32 id) {
    for (GList *l = active_notifications; l != NULL; l = l->next) {
        VenomNotification *notification = (VenomNotification *)l->data;
        if (notification->id == id) {
            return notification;
        }
    }
    return NULL;
}

// دالة مساعدة لإرسال إشارة "تحديث السجل"
void emit_history_updated_signal(GDBusConnection *connection) {
    // استخدم المتغير العام إذا لم نحصل على connection
    if (!connection) connection = dbus_connection;
    if (!connection) return;
    
    g_dbus_connection_emit_signal(connection,
                                  NULL,
                                  "/org/freedesktop/Notifications",
                                  "org.venom.NotificationHistory",
                                  "HistoryUpdated",
                                  NULL, NULL);
}

static gboolean notify_is_wayland_session(void) {
#if defined(GDK_WINDOWING_WAYLAND)
    return GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default());
#else
    return FALSE;
#endif
}

static void on_ui_action(guint32 id, const char *action_key, gpointer user_data) {
    (void)user_data;
    if (dbus_connection) {
        g_dbus_connection_emit_signal(dbus_connection, NULL, "/org/freedesktop/Notifications",
                                      "org.freedesktop.Notifications", "ActionInvoked",
                                      g_variant_new("(us)", id, action_key), NULL);
    }
    close_notification(id, 2); // 2 = Dismissed by user
}

gboolean on_timeout(gpointer data) {
    close_notification(GPOINTER_TO_UINT(data), 1); // 1 = Expired
    return FALSE; 
}

void close_notification(guint32 id, guint reason) {
    GList *l;
    for (l = active_notifications; l != NULL; l = l->next) {
        VenomNotification *n = (VenomNotification *)l->data;
        if (n->id == id) {
            if (n->timeout_source > 0) g_source_remove(n->timeout_source);
            
            notify_ui_destroy(n);
            active_notifications = g_list_delete_link(active_notifications, l);
            g_free(n->app_name);
            g_free(n->icon_path);
            g_free(n);
            notify_ui_reposition(active_notifications, notify_use_layer_shell);
            
            // إرسال إشارة الإغلاق
            if (dbus_connection) {
                g_dbus_connection_emit_signal(dbus_connection, NULL, "/org/freedesktop/Notifications",
                                              "org.freedesktop.Notifications", "NotificationClosed",
                                              g_variant_new("(uu)", id, reason), NULL);
            }
            break;
        }
    }
}

// --- إنشاء الإشعار وعرضه ---
static guint32 create_new_notification(const char *app_name, guint32 replaces_id, const char *summary, const char *body, const char *icon, GVariant *actions, gint timeout) {
    // إذا كان وضع عدم الإزعاج مفعل، أضف للسجل بشكل صامت فقط
    if (do_not_disturb) {
        NotificationItem *existing_item = replaces_id ? find_history_item_by_id(replaces_id) : NULL;
        if (existing_item) {
            g_free(existing_item->app_name);
            g_free(existing_item->icon_path);
            g_free(existing_item->summary);
            g_free(existing_item->body);
            existing_item->app_name = g_strdup(app_name);
            existing_item->icon_path = g_strdup(icon);
            existing_item->summary = g_strdup(summary);
            existing_item->body = g_strdup(body);
            existing_item->timestamp = time(NULL);
            emit_history_updated_signal(NULL);
            return existing_item->id;
        }

        guint32 new_id = id_counter++;
        add_to_history(new_id, app_name, icon, summary, body);
        emit_history_updated_signal(NULL);
        return new_id;
    }

    VenomNotification *existing_notification = replaces_id ? find_active_notification_by_id(replaces_id) : NULL;
    if (existing_notification) {
        NotificationItem *existing_item = find_history_item_by_id(existing_notification->id);
        if (existing_item) {
            g_free(existing_item->app_name);
            g_free(existing_item->icon_path);
            g_free(existing_item->summary);
            g_free(existing_item->body);
            existing_item->app_name = g_strdup(app_name);
            existing_item->icon_path = g_strdup(icon);
            existing_item->summary = g_strdup(summary);
            existing_item->body = g_strdup(body);
            existing_item->timestamp = time(NULL);
        }

        g_free(existing_notification->app_name);
        existing_notification->app_name = g_strdup(app_name);
        g_free(existing_notification->icon_path);
        existing_notification->icon_path = g_strdup(icon);
        notify_ui_update_content(existing_notification, summary, body, icon);
        notify_ui_reposition(active_notifications, notify_use_layer_shell);

        if (existing_notification->timeout_source > 0) {
            g_source_remove(existing_notification->timeout_source);
        }
        if (timeout <= 0) timeout = DEFAULT_TIMEOUT;
        existing_notification->timeout_source = g_timeout_add(timeout, on_timeout, GUINT_TO_POINTER(existing_notification->id));
        emit_history_updated_signal(NULL);
        return existing_notification->id;
    }

    VenomNotification *n = g_new0(VenomNotification, 1);
    n->id = id_counter++;
    n->app_name = g_strdup(app_name);
    n->icon_path = g_strdup(icon);

    notify_ui_setup_window(n, summary, body, icon, actions, notify_use_layer_shell, on_ui_action, NULL);
    
    active_notifications = g_list_append(active_notifications, n);
    
    // إضافة الإشعار للسجل
    add_to_history(n->id, app_name, icon, summary, body);
    
    notify_ui_reposition(active_notifications, notify_use_layer_shell);
    
    // إرسال إشارة تحديث السجل
    emit_history_updated_signal(NULL);

    if (timeout <= 0) timeout = DEFAULT_TIMEOUT;
    n->timeout_source = g_timeout_add(timeout, on_timeout, GUINT_TO_POINTER(n->id));
    return n->id;
}

// --- معالجة اتصالات D-Bus ---
static void handle_method_call(GDBusConnection *connection, const gchar *sender,
                               const gchar *object_path, const gchar *interface_name,
                               const gchar *method_name, GVariant *parameters,
                               GDBusMethodInvocation *invocation, gpointer user_data) {
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)user_data;
    if (g_strcmp0(method_name, "Notify") == 0) {
        gchar *app_name, *app_icon, *summary, *body;
        guint32 replaces_id;
        GVariant *actions = NULL;
        GVariant *hints = NULL;
        gint32 expire_timeout;

        g_variant_get(parameters, "(susss@as@a{sv}i)", 
                      &app_name, &replaces_id, &app_icon, 
                      &summary, &body, &actions, &hints, &expire_timeout);

        guint32 notification_id = create_new_notification(app_name, replaces_id, summary, body, app_icon, actions, expire_timeout);
        
        // إخبار الكونترول سنتر بتحديث السجل
        emit_history_updated_signal(connection);
        
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", notification_id));

        g_free(app_name); g_free(app_icon); g_free(summary); g_free(body);
        if (actions) g_variant_unref(actions);
        if (hints) g_variant_unref(hints);
    }
    // طلب السجل (للكونترول سنتر)
    else if (g_strcmp0(method_name, "GetHistory") == 0) {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ussss)"));
        
        for (GList *l = history_list; l != NULL; l = l->next) {
            NotificationItem *item = (NotificationItem *)l->data;
            g_variant_builder_add(builder, "(ussss)", 
                                  item->id, 
                                  item->app_name ? item->app_name : "",
                                  item->icon_path ? item->icon_path : "",
                                  item->summary ? item->summary : "",
                                  item->body ? item->body : "");
        }
        
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(ussss))", builder));
        g_variant_builder_unref(builder);
    }
    // مسح السجل
    else if (g_strcmp0(method_name, "ClearHistory") == 0) {
        clear_history();
        emit_history_updated_signal(connection);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    // تفعيل/تعطيل وضع عدم الإزعاج
    else if (g_strcmp0(method_name, "SetDoNotDisturb") == 0) {
        gboolean enabled;
        g_variant_get(parameters, "(b)", &enabled);
        do_not_disturb = enabled;
        
        // إرسال إشارة التغيير
        g_dbus_connection_emit_signal(connection,
                                      NULL,
                                      "/org/freedesktop/Notifications",
                                      "org.venom.NotificationHistory",
                                      "DoNotDisturbChanged",
                                      g_variant_new("(b)", do_not_disturb), NULL);
        
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    // الحصول على حالة وضع عدم الإزعاج
    else if (g_strcmp0(method_name, "GetDoNotDisturb") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", do_not_disturb));
    }
    else if (g_strcmp0(method_name, "GetCapabilities") == 0) {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
        g_variant_builder_add(builder, "s", "body");
        g_variant_builder_add(builder, "s", "actions");
        g_variant_builder_add(builder, "s", "body-markup");
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", builder));
        g_variant_builder_unref(builder);
    }
    else if (g_strcmp0(method_name, "GetServerInformation") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(ssss)", "Venom", "Vaxp", "1.0", "1.2"));
    }
    else if (g_strcmp0(method_name, "CloseNotification") == 0) {
        guint32 id;
        g_variant_get(parameters, "(u)", &id);
        close_notification(id, 3);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = handle_method_call,
};

static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)name;
    (void)user_data;
    dbus_connection = connection;
    GDBusNodeInfo *node_info = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications",
                                      node_info->interfaces[0], &interface_vtable, NULL, NULL, NULL);
    g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications",
                                      node_info->interfaces[1], &interface_vtable, NULL, NULL, NULL);
}

#include "notify.h"

void notify_init(void) {
    notify_use_layer_shell = notify_is_wayland_session() && gtk_layer_is_supported();
    notify_ui_init(); // تحميل الاستايل

    g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.Notifications",
                   G_BUS_NAME_OWNER_FLAGS_REPLACE, on_bus_acquired, NULL, NULL, NULL, NULL);
}
