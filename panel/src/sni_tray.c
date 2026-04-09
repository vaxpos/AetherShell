#include "sni_tray.h"

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <libdbusmenu-glib/dbusmenu-glib.h>
#include <string.h>
#include <unistd.h>

#define SNI_WATCHER_NAME  "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH  "/StatusNotifierWatcher"
#define SNI_WATCHER_IFACE "org.kde.StatusNotifierWatcher"
#define SNI_ITEM_IFACE    "org.kde.StatusNotifierItem"
#define DBUS_PROPS_IFACE  "org.freedesktop.DBus.Properties"
#define SNI_LAZY_LOAD_DELAY_MS 1200

typedef struct tray_item
{
    gchar *service;
    gchar *bus_name;
    gchar *object_path;
    gchar *menu_path;

    GtkWidget *button;
    GtkWidget *image;

    GDBusProxy *proxy;
    DbusmenuClient *menu_client;

    guint signal_handler_id;
} tray_item_t;

static GtkWidget *tray_box = NULL;

static GHashTable *tray_items = NULL; /* full service => tray_item_t */
static GHashTable *watcher_items = NULL; /* full service => watch id */
static GHashTable *watcher_hosts = NULL; /* host service => watch id */

static GDBusConnection *session_bus = NULL;
static GDBusProxy *watcher_proxy = NULL;
static GQueue *pending_item_queue = NULL;

static guint watcher_name_id = 0;
static guint watcher_object_id = 0;
static guint watcher_name_watch_id = 0;
static guint host_name_id = 0;
static guint tray_init_timeout_id = 0;
static guint pending_item_source_id = 0;

static gchar *host_name = NULL;
static guint host_counter = 0;
static gboolean tray_backend_started = FALSE;

static const gchar watcher_xml[] =
    "<node>"
    "  <interface name='org.kde.StatusNotifierWatcher'>"
    "    <method name='RegisterStatusNotifierItem'>"
    "      <arg direction='in' name='service' type='s'/>"
    "    </method>"
    "    <method name='RegisterStatusNotifierHost'>"
    "      <arg direction='in' name='service' type='s'/>"
    "    </method>"
    "    <property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
    "    <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
    "    <property name='ProtocolVersion' type='i' access='read'/>"
    "    <signal name='StatusNotifierItemRegistered'>"
    "      <arg name='service' type='s'/>"
    "    </signal>"
    "    <signal name='StatusNotifierItemUnregistered'>"
    "      <arg name='service' type='s'/>"
    "    </signal>"
    "    <signal name='StatusNotifierHostRegistered'/>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo *watcher_node_info = NULL;

static void refresh_registered_items_property(void);
static void tray_add_item(const gchar *service);
static void tray_remove_item(const gchar *service);
static void ensure_host_registered(void);
static gboolean start_tray_backend(gpointer user_data);
static void enqueue_tray_item(const gchar *service);
static gboolean process_pending_items(gpointer user_data);
static void on_watcher_proxy_ready(GObject *source_object, GAsyncResult *res, gpointer user_data);

static void bus_watch_id_free(gpointer data)
{
    guint watch_id = GPOINTER_TO_UINT(data);
    if (watch_id)
    {
        g_bus_unwatch_name(watch_id);
    }
}

static gchar* parse_bus_name(const gchar *service)
{
    const gchar *slash = strchr(service, '/');
    if (!slash)
    {
        return g_strdup(service);
    }

    return g_strndup(service, slash - service);
}

static gchar* parse_object_path(const gchar *service)
{
    const gchar *slash = strchr(service, '/');
    if (!slash)
    {
        return g_strdup("/StatusNotifierItem");
    }

    return g_strdup(slash);
}

static gchar* build_full_service_name(const gchar *sender, const gchar *service)
{
    if (!service || !*service)
    {
        return NULL;
    }

    if (service[0] == '/')
    {
        return g_strconcat(sender, service, NULL);
    }

    if (strchr(service, '/'))
    {
        return g_strdup(service);
    }

    return g_strconcat(service, "/StatusNotifierItem", NULL);
}

static GdkPixbuf* pixbuf_from_sni_data(const guchar *data, gsize len, gint w, gint h)
{
    if (!data || w <= 0 || h <= 0)
    {
        return NULL;
    }

    gsize expected = (gsize)w * (gsize)h * 4;
    if (len < expected)
    {
        return NULL;
    }

    guchar *rgba = g_new(guchar, expected);
    for (gsize i = 0; i < expected; i += 4)
    {
        guchar a = data[i];
        rgba[i]     = data[i + 1];
        rgba[i + 1] = data[i + 2];
        rgba[i + 2] = data[i + 3];
        rgba[i + 3] = a;
    }

    return gdk_pixbuf_new_from_data(
        rgba, GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4,
        (GdkPixbufDestroyNotify)g_free, NULL);
}

static GdkPixbuf* pixbuf_from_variant(GVariant *variant)
{
    if (!variant)
    {
        return NULL;
    }

    GVariantIter iter;
    GVariant *child = NULL;
    gint best_area = -1;
    GdkPixbuf *best = NULL;

    g_variant_iter_init(&iter, variant);
    while ((child = g_variant_iter_next_value(&iter)))
    {
        gint width = 0, height = 0;
        GVariant *bytes = NULL;
        g_variant_get(child, "(ii@ay)", &width, &height, &bytes);

        if (bytes)
        {
            gsize len = 0;
            const guchar *data = g_variant_get_fixed_array(bytes, &len, sizeof(guchar));
            GdkPixbuf *pixbuf = pixbuf_from_sni_data(data, len, width, height);
            gint area = width * height;

            if (pixbuf && area > best_area)
            {
                if (best)
                {
                    g_object_unref(best);
                }

                best = pixbuf;
                best_area = area;
            } else if (pixbuf)
            {
                g_object_unref(pixbuf);
            }

            g_variant_unref(bytes);
        }

        g_variant_unref(child);
    }

    return best;
}

static GVariant* get_cached_or_fetched_property(tray_item_t *item, const gchar *property_name)
{
    if (!item || !item->proxy)
    {
        return NULL;
    }

    GVariant *value = g_dbus_proxy_get_cached_property(item->proxy, property_name);
    if (value)
    {
        return value;
    }

    GError *error = NULL;
    GVariant *result = g_dbus_proxy_call_sync(
        item->proxy, "org.freedesktop.DBus.Properties.Get",
        g_variant_new("(ss)", SNI_ITEM_IFACE, property_name),
        G_DBUS_CALL_FLAGS_NONE, 1500, NULL, &error);

    if (!result)
    {
        if (error)
        {
            g_error_free(error);
        }

        return NULL;
    }

    GVariant *inner = NULL;
    g_variant_get(result, "(v)", &inner);
    g_dbus_proxy_set_cached_property(item->proxy, property_name, inner);
    g_variant_unref(result);
    return inner;
}

static gboolean item_is_passive(tray_item_t *item)
{
    gboolean passive = FALSE;
    GVariant *status = get_cached_or_fetched_property(item, "Status");
    if (status)
    {
        passive = g_strcmp0(g_variant_get_string(status, NULL), "Passive") == 0;
        g_variant_unref(status);
    }

    return passive;
}

static void update_item_tooltip(tray_item_t *item)
{
    GVariant *title = get_cached_or_fetched_property(item, "Title");
    const gchar *text = item->service;

    if (title)
    {
        const gchar *title_text = g_variant_get_string(title, NULL);
        if (title_text && *title_text)
        {
            text = title_text;
        }

        g_variant_unref(title);
    }

    gtk_widget_set_tooltip_text(item->button, text);
}

static void update_item_icon(tray_item_t *item)
{
    if (!item || !item->image)
    {
        return;
    }

    GVariant *status = get_cached_or_fetched_property(item, "Status");
    const gchar *status_value = status ? g_variant_get_string(status, NULL) : "";
    const gchar *icon_prefix =
        g_strcmp0(status_value, "NeedsAttention") == 0 ? "AttentionIcon" : "Icon";

    gchar *name_prop = g_strdup_printf("%sName", icon_prefix);
    gchar *pixmap_prop = g_strdup_printf("%sPixmap", icon_prefix);

    GVariant *icon_name = get_cached_or_fetched_property(item, name_prop);
    GVariant *icon_pixmap = get_cached_or_fetched_property(item, pixmap_prop);
    GdkPixbuf *pixbuf = pixbuf_from_variant(icon_pixmap);

    if (pixbuf)
    {
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, 16, 16, GDK_INTERP_BILINEAR);
        gtk_image_set_from_pixbuf(GTK_IMAGE(item->image), scaled ? scaled : pixbuf);
        if (scaled)
        {
            g_object_unref(scaled);
        }

        g_object_unref(pixbuf);
    } else if (icon_name)
    {
        gtk_image_set_from_icon_name(GTK_IMAGE(item->image),
            g_variant_get_string(icon_name, NULL), GTK_ICON_SIZE_MENU);
    } else
    {
        gtk_image_set_from_icon_name(GTK_IMAGE(item->image),
            "application-x-executable-symbolic", GTK_ICON_SIZE_MENU);
    }

    if (item_is_passive(item))
    {
        gtk_widget_hide(item->button);
    } else
    {
        gtk_widget_show_all(item->button);
    }

    if (icon_pixmap)
    {
        g_variant_unref(icon_pixmap);
    }

    if (icon_name)
    {
        g_variant_unref(icon_name);
    }

    if (status)
    {
        g_variant_unref(status);
    }

    g_free(name_prop);
    g_free(pixmap_prop);
}

static void call_item_method(tray_item_t *item, const gchar *method, gint x, gint y)
{
    if (!item || !item->proxy)
    {
        return;
    }

    g_dbus_proxy_call(item->proxy, method, g_variant_new("(ii)", x, y),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void on_menu_item_activate(GtkMenuItem *menu_item, gpointer user_data)
{
    DbusmenuMenuitem *dbus_item = DBUSMENU_MENUITEM(user_data);
    if (!dbus_item)
    {
        return;
    }

    dbusmenu_menuitem_handle_event(dbus_item, DBUSMENU_MENUITEM_EVENT_ACTIVATED,
        g_variant_new_int32(0), gtk_get_current_event_time());
}

static GtkWidget* build_menu_from_dbusmenu(DbusmenuMenuitem *root);

static GtkWidget* build_menu_item_widget(DbusmenuMenuitem *item)
{
    const gchar *label = dbusmenu_menuitem_property_get(item, DBUSMENU_MENUITEM_PROP_LABEL);
    const gchar *type = dbusmenu_menuitem_property_get(item, DBUSMENU_MENUITEM_PROP_TYPE);

    if (!label || !*label || g_strcmp0(type, DBUSMENU_CLIENT_TYPES_SEPARATOR) == 0)
    {
        return gtk_separator_menu_item_new();
    }

    GtkWidget *menu_item = gtk_menu_item_new_with_label(label);
    gboolean enabled = dbusmenu_menuitem_property_get_bool(item, DBUSMENU_MENUITEM_PROP_ENABLED);
    gtk_widget_set_sensitive(menu_item, enabled);

    if (dbusmenu_menuitem_get_children(item))
    {
        GtkWidget *submenu = build_menu_from_dbusmenu(item);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), submenu);
    } else
    {
        g_signal_connect(menu_item, "activate", G_CALLBACK(on_menu_item_activate), item);
    }

    return menu_item;
}

static GtkWidget* build_menu_from_dbusmenu(DbusmenuMenuitem *root)
{
    GtkWidget *menu = gtk_menu_new();
    GList *children = dbusmenu_menuitem_get_children(root);

    for (GList *it = children; it; it = it->next)
    {
        GtkWidget *menu_item = build_menu_item_widget(DBUSMENU_MENUITEM(it->data));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    }

    gtk_widget_show_all(menu);
    return menu;
}

static GtkWidget* build_context_menu(tray_item_t *item)
{
    if (!item || !item->menu_client)
    {
        return NULL;
    }

    DbusmenuMenuitem *root = dbusmenu_client_get_root(item->menu_client);
    if (!root || !dbusmenu_menuitem_get_children(root))
    {
        return NULL;
    }

    return build_menu_from_dbusmenu(root);
}

static gboolean on_tray_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    tray_item_t *item = user_data;
    if (!item || !item->proxy || event->type != GDK_BUTTON_PRESS)
    {
        return FALSE;
    }

    gint x = (gint)event->x_root;
    gint y = (gint)event->y_root;

    if (event->button == 1)
    {
        GVariant *is_menu = get_cached_or_fetched_property(item, "ItemIsMenu");
        gboolean item_is_menu = is_menu ? g_variant_get_boolean(is_menu) : FALSE;

        if (is_menu)
        {
            g_variant_unref(is_menu);
        }

        if (item_is_menu)
        {
            GtkWidget *menu = build_context_menu(item);
            if (menu)
            {
                gtk_menu_popup_at_widget(GTK_MENU(menu), widget,
                    GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, (GdkEvent*)event);
            } else
            {
                call_item_method(item, "ContextMenu", x, y);
            }
        } else
        {
            call_item_method(item, "Activate", x, y);
        }

        return TRUE;
    }

    if (event->button == 2)
    {
        call_item_method(item, "SecondaryActivate", x, y);
        return TRUE;
    }

    if (event->button == 3)
    {
        GtkWidget *menu = build_context_menu(item);
        if (menu)
        {
            gtk_menu_popup_at_widget(GTK_MENU(menu), widget,
                GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, (GdkEvent*)event);
        } else
        {
            call_item_method(item, "ContextMenu", x, y);
        }

        return TRUE;
    }

    return FALSE;
}

static gboolean on_tray_button_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
    tray_item_t *item = user_data;
    gint delta = 0;
    const gchar *orientation = "vertical";

    (void)widget;

    if (!item || !item->proxy)
    {
        return FALSE;
    }

    if (event->direction == GDK_SCROLL_UP)
    {
        delta = 1;
    } else if (event->direction == GDK_SCROLL_DOWN)
    {
        delta = -1;
    } else if (event->direction == GDK_SCROLL_LEFT)
    {
        delta = -1;
        orientation = "horizontal";
    } else if (event->direction == GDK_SCROLL_RIGHT)
    {
        delta = 1;
        orientation = "horizontal";
    } else
    {
        return FALSE;
    }

    g_dbus_proxy_call(item->proxy, "Scroll", g_variant_new("(is)", delta, orientation),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    return TRUE;
}

static void tray_item_free(gpointer data)
{
    tray_item_t *item = data;
    if (!item)
    {
        return;
    }

    if (item->signal_handler_id && item->proxy)
    {
        g_signal_handler_disconnect(item->proxy, item->signal_handler_id);
    }

    if (item->button)
    {
        gtk_widget_destroy(item->button);
    }

    g_clear_object(&item->proxy);
    g_clear_object(&item->menu_client);

    g_free(item->service);
    g_free(item->bus_name);
    g_free(item->object_path);
    g_free(item->menu_path);
    g_free(item);
}

static void setup_item_menu(tray_item_t *item)
{
    GVariant *menu = get_cached_or_fetched_property(item, "Menu");
    if (!menu)
    {
        return;
    }

    const gchar *menu_path = g_variant_get_string(menu, NULL);
    if (menu_path && *menu_path)
    {
        g_free(item->menu_path);
        item->menu_path = g_strdup(menu_path);
        g_clear_object(&item->menu_client);
        item->menu_client = dbusmenu_client_new(item->bus_name, item->menu_path);
    }

    g_variant_unref(menu);
}

static void handle_item_signal(GDBusProxy *proxy, const gchar *sender_name,
    const gchar *signal_name, GVariant *parameters, gpointer user_data)
{
    tray_item_t *item = user_data;
    (void)proxy;
    (void)sender_name;
    (void)parameters;

    if (!item || !signal_name)
    {
        return;
    }

    if (g_strcmp0(signal_name, "NewIcon") == 0 ||
        g_strcmp0(signal_name, "NewAttentionIcon") == 0 ||
        g_strcmp0(signal_name, "NewStatus") == 0 ||
        g_strcmp0(signal_name, "NewIconThemePath") == 0)
    {
        update_item_icon(item);
    } else if (g_strcmp0(signal_name, "NewTitle") == 0 ||
        g_strcmp0(signal_name, "NewToolTip") == 0)
    {
        update_item_tooltip(item);
    } else if (g_strcmp0(signal_name, "NewMenu") == 0)
    {
        setup_item_menu(item);
    }
}

static void tray_add_item(const gchar *service)
{
    if (!tray_box || !service || !*service)
    {
        return;
    }

    if (!tray_items)
    {
        tray_items = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, tray_item_free);
    }

    if (g_hash_table_contains(tray_items, service))
    {
        return;
    }

    tray_item_t *item = g_new0(tray_item_t, 1);
    item->service = g_strdup(service);
    item->bus_name = parse_bus_name(service);
    item->object_path = parse_object_path(service);

    GError *error = NULL;
    item->proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL,
        item->bus_name, item->object_path, SNI_ITEM_IFACE, NULL, &error);

    if (!item->proxy)
    {
        if (error)
        {
            g_error_free(error);
        }

        tray_item_free(item);
        return;
    }

    item->button = gtk_button_new();
    item->image = gtk_image_new_from_icon_name("application-x-executable-symbolic", GTK_ICON_SIZE_MENU);

    gtk_button_set_relief(GTK_BUTTON(item->button), GTK_RELIEF_NONE);
    gtk_container_add(GTK_CONTAINER(item->button), item->image);
    gtk_widget_add_events(item->button, GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(item->button, "button-press-event", G_CALLBACK(on_tray_button_press), item);
    g_signal_connect(item->button, "scroll-event", G_CALLBACK(on_tray_button_scroll), item);

    GtkStyleContext *ctx = gtk_widget_get_style_context(item->button);
    gtk_style_context_add_class(ctx, "tray-icon-btn");

    item->signal_handler_id = g_signal_connect(item->proxy, "g-signal",
        G_CALLBACK(handle_item_signal), item);

    update_item_tooltip(item);
    update_item_icon(item);
    setup_item_menu(item);

    gtk_box_pack_start(GTK_BOX(tray_box), item->button, FALSE, FALSE, 0);
    gtk_widget_show_all(item->button);

    g_hash_table_insert(tray_items, g_strdup(item->service), item);
}

static void tray_remove_item(const gchar *service)
{
    if (!tray_items || !service)
    {
        return;
    }

    g_hash_table_remove(tray_items, service);
}

static void enqueue_tray_item(const gchar *service)
{
    if (!service || !*service)
    {
        return;
    }

    if (!pending_item_queue)
    {
        pending_item_queue = g_queue_new();
    }

    for (GList *it = pending_item_queue->head; it; it = it->next)
    {
        if (g_strcmp0((const gchar*)it->data, service) == 0)
        {
            return;
        }
    }

    if (tray_items && g_hash_table_contains(tray_items, service))
    {
        return;
    }

    g_queue_push_tail(pending_item_queue, g_strdup(service));
    if (!pending_item_source_id)
    {
        pending_item_source_id = g_idle_add(process_pending_items, NULL);
    }
}

static gboolean process_pending_items(gpointer user_data)
{
    gchar *service = NULL;
    (void)user_data;

    if (!pending_item_queue || g_queue_is_empty(pending_item_queue))
    {
        pending_item_source_id = 0;
        return G_SOURCE_REMOVE;
    }

    service = g_queue_pop_head(pending_item_queue);
    tray_add_item(service);
    g_free(service);

    if (g_queue_is_empty(pending_item_queue))
    {
        pending_item_source_id = 0;
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void add_registered_items_from_variant(GVariant *items)
{
    if (!items)
    {
        return;
    }

    GVariantIter iter;
    const gchar *service = NULL;

    g_variant_iter_init(&iter, items);
    while (g_variant_iter_next(&iter, "&s", &service))
    {
        enqueue_tray_item(service);
    }
}

static void on_watcher_proxy_signal(GDBusProxy *proxy, const gchar *sender_name,
    const gchar *signal_name, GVariant *parameters, gpointer user_data)
{
    const gchar *service = NULL;
    (void)proxy;
    (void)sender_name;
    (void)user_data;

    if (g_strcmp0(signal_name, "StatusNotifierItemRegistered") == 0)
    {
        g_variant_get(parameters, "(&s)", &service);
        enqueue_tray_item(service);
    } else if (g_strcmp0(signal_name, "StatusNotifierItemUnregistered") == 0)
    {
        g_variant_get(parameters, "(&s)", &service);
        tray_remove_item(service);
    }
}

static void on_host_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)user_data;
    ensure_host_registered();
}

static void ensure_host_registered(void)
{
    if (!watcher_proxy || !host_name)
    {
        return;
    }

    g_dbus_proxy_call(watcher_proxy, "RegisterStatusNotifierHost",
        g_variant_new("(s)", host_name),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void watcher_item_vanished(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    gchar *service = user_data;
    (void)connection;
    (void)name;

    if (watcher_items)
    {
        g_hash_table_remove(watcher_items, service);
    }

    tray_remove_item(service);

    if (session_bus)
    {
        g_dbus_connection_emit_signal(session_bus, NULL, SNI_WATCHER_PATH, SNI_WATCHER_IFACE,
            "StatusNotifierItemUnregistered", g_variant_new("(s)", service), NULL);
        refresh_registered_items_property();
    }
}

static void watcher_host_vanished(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    gchar *service = user_data;
    (void)connection;
    (void)name;

    if (watcher_hosts)
    {
        g_hash_table_remove(watcher_hosts, service);
    }

    refresh_registered_items_property();
}

static void emit_properties_changed(const gchar *property_name, GVariant *value)
{
    GVariantBuilder changed_builder;
    GVariantBuilder invalidated_builder;

    g_variant_builder_init(&changed_builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed_builder, "{sv}", property_name, value);

    g_variant_builder_init(&invalidated_builder, G_VARIANT_TYPE("as"));

    g_dbus_connection_emit_signal(session_bus, NULL, SNI_WATCHER_PATH, DBUS_PROPS_IFACE,
        "PropertiesChanged",
        g_variant_new("(sa{sv}as)", SNI_WATCHER_IFACE, &changed_builder, &invalidated_builder),
        NULL);
}

static void refresh_registered_items_property(void)
{
    if (!session_bus)
    {
        return;
    }

    GVariantBuilder items_builder;
    g_variant_builder_init(&items_builder, G_VARIANT_TYPE("as"));

    if (watcher_items)
    {
        GHashTableIter iter;
        gpointer key = NULL;
        gpointer value = NULL;
        g_hash_table_iter_init(&iter, watcher_items);
        while (g_hash_table_iter_next(&iter, &key, &value))
        {
            g_variant_builder_add(&items_builder, "s", (const gchar*)key);
        }
    }

    emit_properties_changed("RegisteredStatusNotifierItems",
        g_variant_builder_end(&items_builder));
    emit_properties_changed("IsStatusNotifierHostRegistered",
        g_variant_new_boolean(watcher_hosts && g_hash_table_size(watcher_hosts) > 0));
}

static void watcher_register_item(const gchar *sender, const gchar *service)
{
    gchar *full_service = build_full_service_name(sender, service);
    if (!full_service)
    {
        return;
    }

    if (!watcher_items)
    {
        watcher_items = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, bus_watch_id_free);
    }

    if (!g_hash_table_contains(watcher_items, full_service))
    {
        gchar *bus_name = parse_bus_name(full_service);
        guint watch_id = g_bus_watch_name(G_BUS_TYPE_SESSION, bus_name,
            G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, watcher_item_vanished, g_strdup(full_service), g_free);
        g_hash_table_insert(watcher_items, g_strdup(full_service), GUINT_TO_POINTER(watch_id));

        g_dbus_connection_emit_signal(session_bus, NULL, SNI_WATCHER_PATH, SNI_WATCHER_IFACE,
            "StatusNotifierItemRegistered", g_variant_new("(s)", full_service), NULL);
        refresh_registered_items_property();
        enqueue_tray_item(full_service);
        g_free(bus_name);
    }

    g_free(full_service);
}

static void watcher_register_host(const gchar *service)
{
    if (!service || !*service)
    {
        return;
    }

    if (!watcher_hosts)
    {
        watcher_hosts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, bus_watch_id_free);
    }

    if (!g_hash_table_contains(watcher_hosts, service))
    {
        guint watch_id = g_bus_watch_name(G_BUS_TYPE_SESSION, service,
            G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, watcher_host_vanished, g_strdup(service), g_free);
        g_hash_table_insert(watcher_hosts, g_strdup(service), GUINT_TO_POINTER(watch_id));

        g_dbus_connection_emit_signal(session_bus, NULL, SNI_WATCHER_PATH, SNI_WATCHER_IFACE,
            "StatusNotifierHostRegistered", NULL, NULL);
        refresh_registered_items_property();
    }
}

static GVariant* watcher_handle_get_property(const gchar *property_name)
{
    if (g_strcmp0(property_name, "RegisteredStatusNotifierItems") == 0)
    {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));

        if (watcher_items)
        {
            GHashTableIter iter;
            gpointer key = NULL;
            gpointer value = NULL;
            g_hash_table_iter_init(&iter, watcher_items);
            while (g_hash_table_iter_next(&iter, &key, &value))
            {
                g_variant_builder_add(&builder, "s", (const gchar*)key);
            }
        }

        return g_variant_builder_end(&builder);
    }

    if (g_strcmp0(property_name, "IsStatusNotifierHostRegistered") == 0)
    {
        return g_variant_new_boolean(watcher_hosts && g_hash_table_size(watcher_hosts) > 0);
    }

    if (g_strcmp0(property_name, "ProtocolVersion") == 0)
    {
        return g_variant_new_int32(0);
    }

    return NULL;
}

static void watcher_method_call(GDBusConnection *connection, const gchar *sender,
    const gchar *object_path, const gchar *interface_name, const gchar *method_name,
    GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data)
{
    gchar *service = NULL;
    (void)connection;
    (void)object_path;
    (void)interface_name;
    (void)user_data;

    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(s)")))
    {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.freedesktop.DBus.Error.InvalidArgs", "Expected a single string argument.");
        return;
    }

    g_variant_get(parameters, "(&s)", &service);

    if (g_strcmp0(method_name, "RegisterStatusNotifierItem") == 0)
    {
        watcher_register_item(sender, service);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (g_strcmp0(method_name, "RegisterStatusNotifierHost") == 0)
    {
        watcher_register_host(service);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    g_dbus_method_invocation_return_dbus_error(invocation,
        "org.freedesktop.DBus.Error.UnknownMethod", "Unknown method.");
}

static GVariant* watcher_get_property(GDBusConnection *connection, const gchar *sender,
    const gchar *object_path, const gchar *interface_name, const gchar *property_name,
    GError **error, gpointer user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)error;
    (void)user_data;

    return watcher_handle_get_property(property_name);
}

static const GDBusInterfaceVTable watcher_vtable = {
    watcher_method_call,
    watcher_get_property,
    NULL,
};

static void on_watcher_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    (void)name;
    (void)user_data;

    session_bus = g_object_ref(connection);

    if (!watcher_node_info)
    {
        GError *error = NULL;
        watcher_node_info = g_dbus_node_info_new_for_xml(watcher_xml, &error);
        if (!watcher_node_info)
        {
            if (error)
            {
                g_error_free(error);
            }

            return;
        }
    }

    watcher_object_id = g_dbus_connection_register_object(connection, SNI_WATCHER_PATH,
        watcher_node_info->interfaces[0], &watcher_vtable, NULL, NULL, NULL);
}

static void clear_watcher_proxy(void)
{
    if (watcher_proxy)
    {
        g_signal_handlers_disconnect_by_func(watcher_proxy, G_CALLBACK(on_watcher_proxy_signal), NULL);
        g_object_unref(watcher_proxy);
        watcher_proxy = NULL;
    }
}

static void clear_tray_box_children(void)
{
    if (!tray_box)
    {
        return;
    }

    GList *children = gtk_container_get_children(GTK_CONTAINER(tray_box));
    for (GList *it = children; it; it = it->next)
    {
        gtk_widget_destroy(GTK_WIDGET(it->data));
    }

    g_list_free(children);
}

static void on_watcher_name_appeared(GDBusConnection *connection, const gchar *name,
    const gchar *owner, gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)owner;
    (void)user_data;

    clear_watcher_proxy();
    g_dbus_proxy_new_for_bus(
        G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL,
        SNI_WATCHER_NAME, SNI_WATCHER_PATH, SNI_WATCHER_IFACE,
        NULL, on_watcher_proxy_ready, NULL);
}

static void on_watcher_proxy_ready(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    GError *error = NULL;
    GVariant *items = NULL;
    (void)source_object;
    (void)user_data;

    watcher_proxy = g_dbus_proxy_new_for_bus_finish(res, &error);
    if (!watcher_proxy)
    {
        if (error)
        {
            g_error_free(error);
        }

        return;
    }

    g_signal_connect(watcher_proxy, "g-signal", G_CALLBACK(on_watcher_proxy_signal), NULL);

    ensure_host_registered();

    items = g_dbus_proxy_get_cached_property(watcher_proxy, "RegisteredStatusNotifierItems");
    add_registered_items_from_variant(items);
    if (items)
    {
        g_variant_unref(items);
    }
}

static void on_watcher_name_vanished(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)user_data;

    clear_watcher_proxy();
    clear_tray_box_children();

    if (tray_items)
    {
        g_hash_table_remove_all(tray_items);
    }
}

GtkWidget* create_sni_tray_widget(void)
{
    if (tray_box)
    {
        return tray_box;
    }

    tray_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    if (!tray_items)
    {
        tray_items = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, tray_item_free);
    }

    if (!host_name)
    {
        host_name = g_strdup_printf("org.kde.StatusNotifierHost-%d-%u", getpid(), ++host_counter);
    }

    if (!tray_init_timeout_id)
    {
        tray_init_timeout_id = g_timeout_add(SNI_LAZY_LOAD_DELAY_MS, start_tray_backend, NULL);
    }

    return tray_box;
}

static gboolean start_tray_backend(gpointer user_data)
{
    (void)user_data;
    tray_init_timeout_id = 0;

    if (tray_backend_started)
    {
        return G_SOURCE_REMOVE;
    }

    tray_backend_started = TRUE;

    if (!watcher_name_id)
    {
        watcher_name_id = g_bus_own_name(G_BUS_TYPE_SESSION, SNI_WATCHER_NAME,
            G_BUS_NAME_OWNER_FLAGS_NONE, on_watcher_bus_acquired, NULL, NULL, NULL, NULL);
    }

    if (!host_name_id)
    {
        host_name_id = g_bus_own_name(G_BUS_TYPE_SESSION, host_name,
            G_BUS_NAME_OWNER_FLAGS_NONE, on_host_name_acquired, NULL, NULL, NULL, NULL);
    }

    if (!watcher_name_watch_id)
    {
        watcher_name_watch_id = g_bus_watch_name(G_BUS_TYPE_SESSION, SNI_WATCHER_NAME,
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            on_watcher_name_appeared, on_watcher_name_vanished, NULL, NULL);
    }

    return G_SOURCE_REMOVE;
}
