/*
 * bluetooth_manager.c
 *
 * Full BlueZ D-Bus backend for AetherShell Panel.
 * Communicates exclusively via org.bluez over GDBus — no bluetoothctl/shell.
 *
 * BlueZ object model:
 *   /org/bluez/hciN              → org.bluez.Adapter1
 *   /org/bluez/hciN/dev_XX_...  → org.bluez.Device1
 */

#include "bluetooth_manager.h"
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* ─────────────────────────────────────────────────────── */
/* Internal state                                          */
/* ─────────────────────────────────────────────────────── */

static GDBusConnection  *bt_bus          = NULL;
static gchar            *adapter_path    = NULL;   /* /org/bluez/hciN */
static gboolean          bt_powered      = FALSE;

/* State callback */
static BtStateCallback   state_cb        = NULL;
static gpointer          state_cb_data   = NULL;

/* Scan callback */
static BtDeviceListCallback scan_cb      = NULL;
static gpointer              scan_cb_data = NULL;

/* D-Bus signal subscriptions */
static guint sig_ifaces_added   = 0;
static guint sig_ifaces_removed = 0;
static guint sig_props_changed  = 0;

/* Scan timeout source */
static guint scan_stop_src = 0;

/* ─────────────────────────────────────────────────────── */
/* Helpers                                                 */
/* ─────────────────────────────────────────────────────── */

static GDBusConnection *get_bus(void)
{
    if (!bt_bus) {
        GError *err = NULL;
        bt_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
        if (!bt_bus) { g_clear_error(&err); }
    }
    return bt_bus;
}

static GVariant *get_property(const gchar *path, const gchar *iface, const gchar *prop)
{
    GDBusConnection *bus = get_bus();
    if (!bus) return NULL;

    GError   *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync(
        bus, BLUEZ_DBUS_SERVICE, path,
        DBUS_PROPS_IFACE, "Get",
        g_variant_new("(ss)", iface, prop),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &err);

    if (!ret) { g_clear_error(&err); return NULL; }
    GVariant *inner = NULL;
    g_variant_get(ret, "(v)", &inner);
    g_variant_unref(ret);
    return inner;
}

static gboolean set_property(const gchar *path, const gchar *iface,
                              const gchar *prop, GVariant *value)
{
    GDBusConnection *bus = get_bus();
    if (!bus) return FALSE;

    GError   *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync(
        bus, BLUEZ_DBUS_SERVICE, path,
        DBUS_PROPS_IFACE, "Set",
        g_variant_new("(ssv)", iface, prop, value),
        NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &err);

    if (!ret) { g_clear_error(&err); return FALSE; }
    g_variant_unref(ret);
    return TRUE;
}

/* ─────────────────────────────────────────────────────── */
/* Device list helpers                                     */
/* ─────────────────────────────────────────────────────── */

void bluetooth_device_free(BtDevice *dev)
{
    if (!dev) return;
    g_free(dev->name);
    g_free(dev->address);
    g_free(dev->object_path);
    g_free(dev->icon);
    g_free(dev);
}

void bluetooth_devices_free(GList *list)
{
    for (GList *l = list; l; l = l->next)
        bluetooth_device_free(l->data);
    g_list_free(list);
}

/* Build a BtDevice from a Device1 property dictionary a{sv} */
static BtDevice *device_from_props(const gchar *object_path, GVariant *props)
{
    if (!props) return NULL;

    BtDevice *dev = g_new0(BtDevice, 1);
    dev->object_path = g_strdup(object_path);

    GVariant *v;

    v = g_variant_lookup_value(props, "Name", G_VARIANT_TYPE_STRING);
    if (v) { dev->name = g_strdup(g_variant_get_string(v, NULL)); g_variant_unref(v); }

    v = g_variant_lookup_value(props, "Address", G_VARIANT_TYPE_STRING);
    if (v) { dev->address = g_strdup(g_variant_get_string(v, NULL)); g_variant_unref(v); }

    v = g_variant_lookup_value(props, "Icon", G_VARIANT_TYPE_STRING);
    if (v) { dev->icon = g_strdup(g_variant_get_string(v, NULL)); g_variant_unref(v); }

    v = g_variant_lookup_value(props, "RSSI", G_VARIANT_TYPE_INT16);
    if (v) { dev->rssi = g_variant_get_int16(v); g_variant_unref(v); }

    v = g_variant_lookup_value(props, "Paired", G_VARIANT_TYPE_BOOLEAN);
    if (v) { dev->paired = g_variant_get_boolean(v); g_variant_unref(v); }

    v = g_variant_lookup_value(props, "Connected", G_VARIANT_TYPE_BOOLEAN);
    if (v) { dev->connected = g_variant_get_boolean(v); g_variant_unref(v); }

    v = g_variant_lookup_value(props, "Trusted", G_VARIANT_TYPE_BOOLEAN);
    if (v) { dev->trusted = g_variant_get_boolean(v); g_variant_unref(v); }

    /* Fallback: use address as name */
    if (!dev->name && dev->address)
        dev->name = g_strdup(dev->address);

    return dev;
}

/* ─────────────────────────────────────────────────────── */
/* GetManagedObjects — discover adapter & devices          */
/* ─────────────────────────────────────────────────────── */

/*
 * Returns: a{oa{sa{sv}}}
 *   object_path → { interface_name → { prop_name → value } }
 */
static GVariant *get_managed_objects(void)
{
    GDBusConnection *bus = get_bus();
    if (!bus) return NULL;

    GError   *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync(
        bus, BLUEZ_DBUS_SERVICE, "/",
        DBUS_OBJMANAGER_IFACE, "GetManagedObjects",
        NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (!ret) { g_clear_error(&err); return NULL; }
    return ret;
}

/* Find the first adapter path and update bt_powered */
static gboolean find_adapter(void)
{
    GVariant *managed = get_managed_objects();
    if (!managed) return FALSE;

    GVariant     *objects = NULL;
    g_variant_get(managed, "(@a{oa{sa{sv}}})", &objects);

    GVariantIter *obj_it  = NULL;
    gchar        *obj_path = NULL;
    GVariant     *iface_dict = NULL;
    gboolean      found   = FALSE;

    g_variant_get(objects, "a{oa{sa{sv}}}", &obj_it);
    while (!found && g_variant_iter_next(obj_it, "{o@a{sa{sv}}}", &obj_path, &iface_dict)) {
        GVariant *adapter_props = g_variant_lookup_value(iface_dict, BLUEZ_ADAPTER_IFACE,
                                                         G_VARIANT_TYPE("a{sv}"));
        if (adapter_props) {
            g_free(adapter_path);
            adapter_path = g_strdup(obj_path);

            GVariant *pw = g_variant_lookup_value(adapter_props, "Powered", G_VARIANT_TYPE_BOOLEAN);
            if (pw) { bt_powered = g_variant_get_boolean(pw); g_variant_unref(pw); }

            g_variant_unref(adapter_props);
            found = TRUE;
        }
        g_free(obj_path);
        g_variant_unref(iface_dict);
    }
    g_variant_iter_free(obj_it);
    g_variant_unref(objects);
    g_variant_unref(managed);
    return found;
}

/* build_device_list() removed — use build_device_list_sorted() everywhere */

static gint bt_device_cmp(gconstpointer a, gconstpointer b)
{
    const BtDevice *da = a, *db = b;
    if (da->connected != db->connected) return db->connected - da->connected;
    if (da->paired    != db->paired)    return db->paired    - da->paired;
    return g_strcmp0(da->name, db->name);
}

static GList *build_device_list_sorted(void)
{
    GVariant *managed = get_managed_objects();
    if (!managed) return NULL;

    GVariant     *objects = NULL;
    g_variant_get(managed, "(@a{oa{sa{sv}}})", &objects);

    GVariantIter *obj_it    = NULL;
    gchar        *obj_path  = NULL;
    GVariant     *iface_dict = NULL;
    GList        *result    = NULL;

    g_variant_get(objects, "a{oa{sa{sv}}}", &obj_it);
    while (g_variant_iter_next(obj_it, "{o@a{sa{sv}}}", &obj_path, &iface_dict)) {
        GVariant *dev_props = g_variant_lookup_value(iface_dict, BLUEZ_DEVICE_IFACE,
                                                     G_VARIANT_TYPE("a{sv}"));
        if (dev_props) {
            if (!adapter_path || g_str_has_prefix(obj_path, adapter_path)) {
                BtDevice *dev = device_from_props(obj_path, dev_props);
                if (dev) result = g_list_append(result, dev);
            }
            g_variant_unref(dev_props);
        }
        g_free(obj_path);
        g_variant_unref(iface_dict);
    }
    g_variant_iter_free(obj_it);
    g_variant_unref(objects);
    g_variant_unref(managed);

    return g_list_sort(result, bt_device_cmp);
}

/* ─────────────────────────────────────────────────────── */
/* BlueZ Agent1 implementation                             */
/* ─────────────────────────────────────────────────────── */

#define AGENT_PATH       "/org/aethershell/btagent"
#define AGENT_CAPABILITY "DisplayYesNo"
#define BLUEZ_AGENTMGR_IFACE "org.bluez.AgentManager1"
#define BLUEZ_AGENTMGR_PATH  "/org/bluez"

static guint agent_registration_id = 0;

/* Introspection XML for org.bluez.Agent1 */
static const gchar agent_xml[] =
    "<node>"
    "  <interface name='org.bluez.Agent1'>"
    "    <method name='Release'/>"
    "    <method name='RequestPinCode'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='s' name='pincode' direction='out'/>"
    "    </method>"
    "    <method name='DisplayPinCode'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='s' name='pincode' direction='in'/>"
    "    </method>"
    "    <method name='RequestPasskey'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='u' name='passkey' direction='out'/>"
    "    </method>"
    "    <method name='DisplayPasskey'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='u' name='passkey' direction='in'/>"
    "      <arg type='q' name='entered' direction='in'/>"
    "    </method>"
    "    <method name='RequestConfirmation'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='u' name='passkey' direction='in'/>"
    "    </method>"
    "    <method name='RequestAuthorization'>"
    "      <arg type='o' name='device' direction='in'/>"
    "    </method>"
    "    <method name='AuthorizeService'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='s' name='uuid' direction='in'/>"
    "    </method>"
    "    <method name='Cancel'/>"
    "  </interface>"
    "</node>";

/* ── Pending confirmation invocation (one at a time) ─── */
static GDBusMethodInvocation *pending_confirmation = NULL;

static void on_confirm_dialog_response(GtkDialog *dialog, gint response, gpointer user_data)
{
    GDBusMethodInvocation *inv = user_data;
    (void)dialog;
    if (response == GTK_RESPONSE_ACCEPT) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("()"));
    } else {
        g_dbus_method_invocation_return_dbus_error(
            inv, "org.bluez.Error.Rejected", "Pairing rejected by user");
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
    pending_confirmation = NULL;
}

/* Show the pairing confirmation dialog (runs in GTK main loop) */
typedef struct { GDBusMethodInvocation *inv; guint32 passkey; gchar *device_path; } ConfirmData;

static gboolean show_confirm_dialog_idle(gpointer user_data)
{
    ConfirmData *d = user_data;

    /* If a previous dialog is still up, reject the new one */
    if (pending_confirmation) {
        g_dbus_method_invocation_return_dbus_error(
            d->inv, "org.bluez.Error.Busy", "Another pairing in progress");
        g_free(d->device_path);
        g_free(d);
        return G_SOURCE_REMOVE;
    }

    pending_confirmation = d->inv;

    /* Build dialog */
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Bluetooth Pairing Request",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel",  GTK_RESPONSE_REJECT,
        "Confirm", GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 12);

    /* Device path hint */
    gchar *hint = g_strdup_printf("Device: %s", d->device_path ? d->device_path : "Unknown");
    GtkWidget *dev_lbl = gtk_label_new(hint);
    gtk_widget_set_halign(dev_lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), dev_lbl, FALSE, FALSE, 0);
    g_free(hint);

    /* Passkey */
    gchar *key_txt = g_strdup_printf("Confirm passkey:  <b>%06u</b>", d->passkey);
    GtkWidget *key_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(key_lbl), key_txt);
    gtk_widget_set_halign(key_lbl, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(content), key_lbl, FALSE, FALSE, 0);
    g_free(key_txt);

    gtk_widget_show_all(content);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(on_confirm_dialog_response), d->inv);
    gtk_window_present(GTK_WINDOW(dialog));

    g_free(d->device_path);
    g_free(d);
    return G_SOURCE_REMOVE;
}

/* ── Agent method call handler ──────────────────────────── */

static void agent_method_call(GDBusConnection       *conn,
                              const gchar           *sender,
                              const gchar           *object_path,
                              const gchar           *interface_name,
                              const gchar           *method_name,
                              GVariant              *parameters,
                              GDBusMethodInvocation *invocation,
                              gpointer               user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)user_data;

    if (g_strcmp0(method_name, "Release") == 0) {
        /* BlueZ unregistered our agent */
        g_dbus_method_invocation_return_value(invocation, NULL);

    } else if (g_strcmp0(method_name, "Cancel") == 0) {
        /* Ongoing pairing was cancelled by BlueZ */
        if (pending_confirmation) {
            /* The dialog response callback will clean it up */
            pending_confirmation = NULL;
        }
        g_dbus_method_invocation_return_value(invocation, NULL);

    } else if (g_strcmp0(method_name, "RequestConfirmation") == 0) {
        /* BlueZ wants us to confirm a numeric passkey */
        gchar   *device = NULL;
        guint32  passkey = 0;
        g_variant_get(parameters, "(ou)", &device, &passkey);

        /* Schedule the GTK dialog on the main loop (we may be called from a
         * GDBus dispatch which can handle a nested loop, but g_idle_add is
         * cleaner and avoids re-entrancy issues). */
        ConfirmData *cd   = g_new0(ConfirmData, 1);
        cd->inv           = invocation;   /* kept alive; we reply later */
        cd->passkey       = passkey;
        cd->device_path   = g_strdup(device);
        g_free(device);

        g_idle_add(show_confirm_dialog_idle, cd);
        /* Do NOT call return_value here — dialog callback will do it */

    } else if (g_strcmp0(method_name, "RequestAuthorization") == 0) {
        /* Just authorize unconditionally (user already chose to connect) */
        g_dbus_method_invocation_return_value(invocation, NULL);

    } else if (g_strcmp0(method_name, "AuthorizeService") == 0) {
        /* Accept all service authorisation requests */
        g_dbus_method_invocation_return_value(invocation, NULL);

    } else if (g_strcmp0(method_name, "RequestPinCode") == 0) {
        /* Return a default PIN — rarely called for modern devices */
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(s)", "0000"));

    } else if (g_strcmp0(method_name, "RequestPasskey") == 0) {
        /* Return passkey 0 — rarely called for modern devices */
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(u)", 0));

    } else if (g_strcmp0(method_name, "DisplayPinCode") == 0 ||
               g_strcmp0(method_name, "DisplayPasskey") == 0) {
        /* Nothing to return for display-only methods */
        g_dbus_method_invocation_return_value(invocation, NULL);

    } else {
        g_dbus_method_invocation_return_error(invocation,
            G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method: %s", method_name);
    }
}

static const GDBusInterfaceVTable agent_vtable = {
    agent_method_call,
    NULL,   /* get_property */
    NULL,   /* set_property */
    { 0 }
};

/* Register the agent with BlueZ and make it the default */
static void register_bluez_agent(GDBusConnection *bus)
{
    if (agent_registration_id) return;  /* already registered */

    GError              *err   = NULL;
    GDBusNodeInfo       *info  = g_dbus_node_info_new_for_xml(agent_xml, &err);
    if (!info) { g_clear_error(&err); return; }

    agent_registration_id = g_dbus_connection_register_object(
        bus,
        AGENT_PATH,
        info->interfaces[0],
        &agent_vtable,
        NULL, NULL, &err);
    g_dbus_node_info_unref(info);
    if (!agent_registration_id) { g_clear_error(&err); return; }

    /* AgentManager1.RegisterAgent */
    GVariant *r1 = g_dbus_connection_call_sync(
        bus, BLUEZ_DBUS_SERVICE, BLUEZ_AGENTMGR_PATH,
        BLUEZ_AGENTMGR_IFACE, "RegisterAgent",
        g_variant_new("(os)", AGENT_PATH, AGENT_CAPABILITY),
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
    if (r1) g_variant_unref(r1);
    g_clear_error(&err);

    /* AgentManager1.RequestDefaultAgent — makes us the preferred agent */
    GVariant *r2 = g_dbus_connection_call_sync(
        bus, BLUEZ_DBUS_SERVICE, BLUEZ_AGENTMGR_PATH,
        BLUEZ_AGENTMGR_IFACE, "RequestDefaultAgent",
        g_variant_new("(o)", AGENT_PATH),
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
    if (r2) g_variant_unref(r2);
    g_clear_error(&err);
}

/* ─────────────────────────────────────────────────────── */
/* D-Bus signal handlers                                   */
/* ─────────────────────────────────────────────────────── */

static void notify_scan_cb(void)
{
    if (!scan_cb) return;
    GList *devs = build_device_list_sorted();
    scan_cb(devs, scan_cb_data);
    /* caller owns the list */
}

/* InterfacesAdded: a new BT device appeared during scan */
static void on_interfaces_added(GDBusConnection *conn,
                                const gchar *sender,
                                const gchar *object_path,
                                const gchar *iface_name,
                                const gchar *signal_name,
                                GVariant    *parameters,
                                gpointer     user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)iface_name; (void)signal_name; (void)user_data;

    gchar    *path  = NULL;
    GVariant *ifaces = NULL;
    g_variant_get(parameters, "(o@a{sa{sv}})", &path, &ifaces);

    GVariant *dev_props = g_variant_lookup_value(ifaces, BLUEZ_DEVICE_IFACE,
                                                 G_VARIANT_TYPE("a{sv}"));
    if (dev_props) {
        g_variant_unref(dev_props);
        notify_scan_cb();
    }

    g_free(path);
    g_variant_unref(ifaces);
}

/* InterfacesRemoved: a device was forgotten / removed */
static void on_interfaces_removed(GDBusConnection *conn,
                                  const gchar *sender,
                                  const gchar *object_path,
                                  const gchar *iface_name,
                                  const gchar *signal_name,
                                  GVariant    *parameters,
                                  gpointer     user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)iface_name; (void)signal_name; (void)user_data;

    gchar    *path  = NULL;
    GVariant *ifaces = NULL;
    g_variant_get(parameters, "(o@as)", &path, &ifaces);

    GVariantIter *it  = NULL;
    gchar        *ifc = NULL;
    g_variant_get(ifaces, "as", &it);
    gboolean is_device = FALSE;
    while (g_variant_iter_next(it, "s", &ifc)) {
        if (g_strcmp0(ifc, BLUEZ_DEVICE_IFACE) == 0) is_device = TRUE;
        g_free(ifc);
    }
    g_variant_iter_free(it);

    if (is_device) notify_scan_cb();

    g_free(path);
    g_variant_unref(ifaces);
}

/*
 * PropertiesChanged on adapter or device.
 * Handles: Adapter1.Powered changes + Device1.Connected/Paired changes.
 */
static void on_properties_changed(GDBusConnection *conn,
                                  const gchar *sender,
                                  const gchar *object_path,
                                  const gchar *iface_name,
                                  const gchar *signal_name,
                                  GVariant    *parameters,
                                  gpointer     user_data)
{
    (void)conn; (void)sender; (void)iface_name; (void)signal_name; (void)user_data;

    gchar    *changed_iface = NULL;
    GVariant *changed_props = NULL;
    g_variant_get(parameters, "(&s@a{sv}as)", &changed_iface, &changed_props, NULL);
    if (!changed_props) return;

    /* ── Adapter powered state ── */
    if (g_strcmp0(changed_iface, BLUEZ_ADAPTER_IFACE) == 0) {
        GVariant *pw = g_variant_lookup_value(changed_props, "Powered", G_VARIANT_TYPE_BOOLEAN);
        if (pw) {
            bt_powered = g_variant_get_boolean(pw);
            g_variant_unref(pw);
            if (state_cb) state_cb(bt_powered, state_cb_data);
        }
    }

    /* ── Device property change (Connected, Paired, RSSI) ── */
    if (g_strcmp0(changed_iface, BLUEZ_DEVICE_IFACE) == 0) {
        GVariant *cv = g_variant_lookup_value(changed_props, "Connected", G_VARIANT_TYPE_BOOLEAN);
        GVariant *pv = g_variant_lookup_value(changed_props, "Paired",    G_VARIANT_TYPE_BOOLEAN);
        GVariant *rv = g_variant_lookup_value(changed_props, "RSSI",      G_VARIANT_TYPE_INT16);
        if (cv || pv || rv) {
            if (cv) g_variant_unref(cv);
            if (pv) g_variant_unref(pv);
            if (rv) g_variant_unref(rv);
            if (state_cb) state_cb(bt_powered, state_cb_data);
            notify_scan_cb();
        }
    }

    g_variant_unref(changed_props);
}

/* ─────────────────────────────────────────────────────── */
/* Public: bluetooth_init                                  */
/* ─────────────────────────────────────────────────────── */

void bluetooth_init(BtStateCallback cb, gpointer user_data)
{
    state_cb      = cb;
    state_cb_data = user_data;

    GDBusConnection *bus = get_bus();
    if (!bus) {
        if (cb) cb(FALSE, user_data);
        return;
    }

    find_adapter();

    /* Subscribe to ObjectManager signals on the root "/" path */
    if (!sig_ifaces_added) {
        sig_ifaces_added = g_dbus_connection_signal_subscribe(
            bus, BLUEZ_DBUS_SERVICE,
            DBUS_OBJMANAGER_IFACE, "InterfacesAdded", "/",
            NULL, G_DBUS_SIGNAL_FLAGS_NONE,
            on_interfaces_added, NULL, NULL);

        sig_ifaces_removed = g_dbus_connection_signal_subscribe(
            bus, BLUEZ_DBUS_SERVICE,
            DBUS_OBJMANAGER_IFACE, "InterfacesRemoved", "/",
            NULL, G_DBUS_SIGNAL_FLAGS_NONE,
            on_interfaces_removed, NULL, NULL);

        /* PropertiesChanged at "/" (path=NULL) catches all objects */
        sig_props_changed = g_dbus_connection_signal_subscribe(
            bus, BLUEZ_DBUS_SERVICE,
            DBUS_PROPS_IFACE, "PropertiesChanged", NULL,
            NULL, G_DBUS_SIGNAL_FLAGS_NONE,
            on_properties_changed, NULL, NULL);
    }

    if (cb) cb(bt_powered, user_data);

    /* Register BlueZ Agent so pairing dialogs work */
    register_bluez_agent(bus);
}

/* ─────────────────────────────────────────────────────── */
/* Public: bluetooth_is_powered                            */
/* ─────────────────────────────────────────────────────── */
/* rfkill helper                                           */
/* ─────────────────────────────────────────────────────── */

/*
 * rfkill_set_bluetooth:
 * Try to set the rfkill soft-block state for the bluetooth device
 * by writing to /sys/class/rfkill/rfkillN/soft.
 *
 * Returns TRUE on success, FALSE if permission denied or device not found.
 * Requires the user to be in the 'rfkill' group (set up via udev rule:
 *   SUBSYSTEM=="rfkill", GROUP="rfkill", MODE="0664"
 * and: sudo usermod -aG rfkill $USER)
 */
static gboolean rfkill_set_bluetooth(gboolean unblock)
{
    GDir   *dir     = g_dir_open("/sys/class/rfkill", 0, NULL);
    if (!dir) return FALSE;

    gboolean done = FALSE;
    const gchar *entry;

    while (!done && (entry = g_dir_read_name(dir))) {
        /* Check device type */
        gchar *type_path = g_strdup_printf("/sys/class/rfkill/%s/type", entry);
        gchar *type_str  = NULL;
        g_file_get_contents(type_path, &type_str, NULL, NULL);
        g_free(type_path);

        if (!type_str) continue;
        gboolean is_bt = g_str_has_prefix(type_str, "bluetooth");
        g_free(type_str);
        if (!is_bt) continue;

        /* Write soft-block state: 0 = unblock, 1 = block */
        gchar *soft_path = g_strdup_printf("/sys/class/rfkill/%s/soft", entry);
        FILE  *f         = fopen(soft_path, "w");
        g_free(soft_path);

        if (f) {
            fprintf(f, "%d", unblock ? 0 : 1);
            fclose(f);
            done = TRUE;
        }
        /* else: permission denied — user needs rfkill group */
    }
    g_dir_close(dir);
    return done;
}

/* ─────────────────────────────────────────────────────── */
/* Public: bluetooth_is_powered                            */
/* ─────────────────────────────────────────────────────── */

gboolean bluetooth_is_powered(void)
{
    return bt_powered;
}

/* ─────────────────────────────────────────────────────── */
/* Public: bluetooth_toggle                                */
/* ─────────────────────────────────────────────────────── */

void bluetooth_toggle(void)
{
    if (!adapter_path) {
        if (!find_adapter() || !adapter_path) return;
    }
    gboolean next = !bt_powered;

    /* If powering ON: unblock rfkill first, otherwise BlueZ rejects it */
    if (next) rfkill_set_bluetooth(TRUE);
    else      rfkill_set_bluetooth(FALSE);

    /* Optimistic local update — the signal will confirm */
    bt_powered = next;
    if (state_cb) state_cb(bt_powered, state_cb_data);

    set_property(adapter_path, BLUEZ_ADAPTER_IFACE, "Powered",
                 g_variant_new_boolean(next));
}

/* ─────────────────────────────────────────────────────── */
/* Public: bluetooth_scan / bluetooth_scan_stop            */
/* ─────────────────────────────────────────────────────── */

static gboolean on_scan_timeout(gpointer user_data)
{
    (void)user_data;
    bluetooth_scan_stop();
    scan_stop_src = 0;
    return G_SOURCE_REMOVE;
}

void bluetooth_scan(BtDeviceListCallback cb, gpointer user_data)
{
    scan_cb      = cb;
    scan_cb_data = user_data;

    if (!adapter_path) {
        if (!find_adapter() || !adapter_path) {
            if (cb) cb(NULL, user_data);
            return;
        }
    }

    if (!bt_powered) {
        /* BT is off — just return empty list */
        if (cb) cb(NULL, user_data);
        return;
    }

    /* Return current cached list immediately */
    notify_scan_cb();

    /* Start discovery */
    GDBusConnection *bus = get_bus();
    GError *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync(
        bus, BLUEZ_DBUS_SERVICE, adapter_path,
        BLUEZ_ADAPTER_IFACE, "StartDiscovery",
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
    if (ret) g_variant_unref(ret);
    g_clear_error(&err);  /* "Already discovering" is not fatal */

    /* Stop after 12 seconds */
    if (scan_stop_src) g_source_remove(scan_stop_src);
    scan_stop_src = g_timeout_add_seconds(12, on_scan_timeout, NULL);
}

void bluetooth_scan_stop(void)
{
    if (!adapter_path) return;
    GDBusConnection *bus = get_bus();
    if (!bus) return;

    GError   *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync(
        bus, BLUEZ_DBUS_SERVICE, adapter_path,
        BLUEZ_ADAPTER_IFACE, "StopDiscovery",
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
    if (ret) g_variant_unref(ret);
    g_clear_error(&err);
}

/* ─────────────────────────────────────────────────────── */
/* Pair helper — async, watches PropertiesChanged          */
/* ─────────────────────────────────────────────────────── */

typedef struct {
    BtConnectCallback cb;
    gpointer          user_data;
    gchar            *device_path;
    guint             sig_id;
    guint             timeout_id;
    gboolean          want_connect_after; /* call Connect() after Pair completes */
} BtOpCtx;

static void bt_op_ctx_free(BtOpCtx *ctx)
{
    if (ctx->timeout_id) { g_source_remove(ctx->timeout_id); ctx->timeout_id = 0; }
    if (ctx->sig_id && bt_bus) {
        g_dbus_connection_signal_unsubscribe(bt_bus, ctx->sig_id);
        ctx->sig_id = 0;
    }
    g_free(ctx->device_path);
    g_free(ctx);
}

/* Forward declarations */
static void do_connect(const gchar *device_path, BtConnectCallback cb, gpointer user_data);

static gboolean on_bt_op_timeout(gpointer user_data)
{
    BtOpCtx *ctx = user_data;
    ctx->timeout_id = 0;
    if (ctx->sig_id && bt_bus)
        g_dbus_connection_signal_unsubscribe(bt_bus, ctx->sig_id);
    ctx->sig_id = 0;
    if (ctx->cb) ctx->cb(FALSE, ctx->user_data);
    bt_op_ctx_free(ctx);
    return G_SOURCE_REMOVE;
}

static void on_pair_props_changed(GDBusConnection *conn,
                                  const gchar *sender,
                                  const gchar *object_path,
                                  const gchar *iface_name,
                                  const gchar *signal_name,
                                  GVariant    *parameters,
                                  gpointer     user_data)
{
    (void)conn; (void)sender; (void)iface_name; (void)signal_name;
    BtOpCtx *ctx = user_data;
    if (g_strcmp0(object_path, ctx->device_path) != 0) return;

    gchar    *changed_iface = NULL;
    GVariant *changed_props = NULL;
    g_variant_get(parameters, "(&s@a{sv}as)", &changed_iface, &changed_props, NULL);
    if (!changed_props) return;

    if (g_strcmp0(changed_iface, BLUEZ_DEVICE_IFACE) == 0) {
        GVariant *pv = g_variant_lookup_value(changed_props, "Paired", G_VARIANT_TYPE_BOOLEAN);
        if (pv) {
            gboolean paired = g_variant_get_boolean(pv);
            g_variant_unref(pv);
            if (paired) {
                if (ctx->sig_id) {
                    g_dbus_connection_signal_unsubscribe(bt_bus, ctx->sig_id);
                    ctx->sig_id = 0;
                }
                BtConnectCallback cb        = ctx->cb;
                gpointer          ud        = ctx->user_data;
                gchar            *dev_path  = g_strdup(ctx->device_path);
                gboolean          do_conn   = ctx->want_connect_after;
                bt_op_ctx_free(ctx);
                if (do_conn) {
                    do_connect(dev_path, cb, ud);
                } else {
                    if (cb) cb(TRUE, ud);
                }
                g_free(dev_path);
            }
        }
    }
    g_variant_unref(changed_props);
}

static void on_connect_props_changed(GDBusConnection *conn,
                                     const gchar *sender,
                                     const gchar *object_path,
                                     const gchar *iface_name,
                                     const gchar *signal_name,
                                     GVariant    *parameters,
                                     gpointer     user_data)
{
    (void)conn; (void)sender; (void)iface_name; (void)signal_name;
    BtOpCtx *ctx = user_data;
    if (g_strcmp0(object_path, ctx->device_path) != 0) return;

    gchar    *changed_iface = NULL;
    GVariant *changed_props = NULL;
    g_variant_get(parameters, "(&s@a{sv}as)", &changed_iface, &changed_props, NULL);
    if (!changed_props) return;

    if (g_strcmp0(changed_iface, BLUEZ_DEVICE_IFACE) == 0) {
        GVariant *cv = g_variant_lookup_value(changed_props, "Connected", G_VARIANT_TYPE_BOOLEAN);
        if (cv) {
            gboolean connected = g_variant_get_boolean(cv);
            g_variant_unref(cv);
            if (connected) {
                BtConnectCallback cb = ctx->cb;
                gpointer          ud = ctx->user_data;
                bt_op_ctx_free(ctx);
                if (cb) cb(TRUE, ud);
            }
        }
    }
    g_variant_unref(changed_props);
}

/* ─────────────────────────────────────────────────────── */
/* Public: bluetooth_pair                                  */
/* ─────────────────────────────────────────────────────── */

/* Async callback for Pair() — just ignore the return value; result comes
 * via PropertiesChanged(Paired=true) which on_pair_props_changed handles. */
static void on_pair_call_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
    (void)source;
    BtOpCtx *ctx = user_data;
    GError  *err = NULL;
    GVariant *ret = g_dbus_connection_call_finish(get_bus(), res, &err);
    if (ret) {
        g_variant_unref(ret);
        /* Success path: PropertiesChanged will fire next and free ctx */
    } else {
        /* Error (e.g. rejected, already paired, agent not available) */
        g_clear_error(&err);
        if (ctx->sig_id && bt_bus) {
            g_dbus_connection_signal_unsubscribe(bt_bus, ctx->sig_id);
            ctx->sig_id = 0;
        }
        BtConnectCallback cb = ctx->cb;
        gpointer          ud = ctx->user_data;
        bt_op_ctx_free(ctx);
        if (cb) cb(FALSE, ud);
    }
}

void bluetooth_pair(const gchar *device_path,
                    BtConnectCallback cb, gpointer user_data)
{
    GDBusConnection *bus = get_bus();
    if (!bus || !device_path) { if (cb) cb(FALSE, user_data); return; }

    BtOpCtx *ctx           = g_new0(BtOpCtx, 1);
    ctx->cb                 = cb;
    ctx->user_data          = user_data;
    ctx->device_path        = g_strdup(device_path);
    ctx->want_connect_after = FALSE;

    /* Watch Paired property change */
    ctx->sig_id = g_dbus_connection_signal_subscribe(
        bus, BLUEZ_DBUS_SERVICE,
        DBUS_PROPS_IFACE, "PropertiesChanged",
        device_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        on_pair_props_changed, ctx, NULL);

    ctx->timeout_id = g_timeout_add_seconds(30, on_bt_op_timeout, ctx);

    /* Fire Pair() asynchronously — does NOT block the GTK main loop */
    g_dbus_connection_call(
        bus, BLUEZ_DBUS_SERVICE, device_path,
        BLUEZ_DEVICE_IFACE, "Pair",
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 30000, NULL,
        on_pair_call_done, ctx);
}

/* ─────────────────────────────────────────────────────── */
/* Public: bluetooth_connect                               */
/* ─────────────────────────────────────────────────────── */

/* Async callback for Connect() */
static void on_connect_call_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
    (void)source;
    BtOpCtx *ctx = user_data;
    GError  *err = NULL;
    GVariant *ret = g_dbus_connection_call_finish(get_bus(), res, &err);
    if (ret) {
        g_variant_unref(ret);
        /* PropertiesChanged(Connected=true) will finalize ctx */
    } else {
        g_clear_error(&err);
        if (ctx->sig_id && bt_bus) {
            g_dbus_connection_signal_unsubscribe(bt_bus, ctx->sig_id);
            ctx->sig_id = 0;
        }
        BtConnectCallback cb = ctx->cb;
        gpointer          ud = ctx->user_data;
        bt_op_ctx_free(ctx);
        if (cb) cb(FALSE, ud);
    }
}

static void do_connect(const gchar *device_path,
                       BtConnectCallback cb, gpointer user_data)
{
    GDBusConnection *bus = get_bus();
    if (!bus || !device_path) { if (cb) cb(FALSE, user_data); return; }

    BtOpCtx *ctx    = g_new0(BtOpCtx, 1);
    ctx->cb          = cb;
    ctx->user_data   = user_data;
    ctx->device_path = g_strdup(device_path);

    /* Watch Connected property change */
    ctx->sig_id = g_dbus_connection_signal_subscribe(
        bus, BLUEZ_DBUS_SERVICE,
        DBUS_PROPS_IFACE, "PropertiesChanged",
        device_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        on_connect_props_changed, ctx, NULL);

    ctx->timeout_id = g_timeout_add_seconds(20, on_bt_op_timeout, ctx);

    /* Fire Connect() asynchronously — does NOT block the GTK main loop */
    g_dbus_connection_call(
        bus, BLUEZ_DBUS_SERVICE, device_path,
        BLUEZ_DEVICE_IFACE, "Connect",
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 20000, NULL,
        on_connect_call_done, ctx);
}

void bluetooth_connect(const gchar *device_path,
                       BtConnectCallback cb, gpointer user_data)
{
    g_print("bluetooth_connect called for path: %s\n", device_path ? device_path : "NULL");
    if (!device_path) { if (cb) cb(FALSE, user_data); return; }

    /* Check if already paired */
    GVariant *pv = get_property(device_path, BLUEZ_DEVICE_IFACE, "Paired");
    gboolean  paired = pv ? g_variant_get_boolean(pv) : FALSE;
    if (pv) g_variant_unref(pv);
    
    g_print("Device paired status: %s\n", paired ? "true" : "false");

    if (!paired) {
        /* Not paired yet — pair first, then connect automatically */
        GDBusConnection *bus = get_bus();
        if (!bus) { if (cb) cb(FALSE, user_data); return; }

        BtOpCtx *ctx           = g_new0(BtOpCtx, 1);
        ctx->cb                 = cb;
        ctx->user_data          = user_data;
        ctx->device_path        = g_strdup(device_path);
        ctx->want_connect_after = TRUE;  /* do_connect() called after Paired=true */

        ctx->sig_id = g_dbus_connection_signal_subscribe(
            bus, BLUEZ_DBUS_SERVICE,
            DBUS_PROPS_IFACE, "PropertiesChanged",
            device_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
            on_pair_props_changed, ctx, NULL);

        ctx->timeout_id = g_timeout_add_seconds(30, on_bt_op_timeout, ctx);

        /* Async — returns immediately, result via signal + on_pair_call_done */
        g_dbus_connection_call(
            bus, BLUEZ_DBUS_SERVICE, device_path,
            BLUEZ_DEVICE_IFACE, "Pair",
            NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 30000, NULL,
            on_pair_call_done, ctx);
    } else {
        /* Already paired — connect directly */
        do_connect(device_path, cb, user_data);
    }
}

/* ─────────────────────────────────────────────────────── */
/* Public: bluetooth_disconnect                            */
/* ─────────────────────────────────────────────────────── */

void bluetooth_disconnect(const gchar *device_path)
{
    GDBusConnection *bus = get_bus();
    if (!bus || !device_path) return;

    GError   *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync(
        bus, BLUEZ_DBUS_SERVICE, device_path,
        BLUEZ_DEVICE_IFACE, "Disconnect",
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &err);
    if (ret) g_variant_unref(ret);
    g_clear_error(&err);
}

/* ─────────────────────────────────────────────────────── */
/* Public: bluetooth_remove                                */
/* ─────────────────────────────────────────────────────── */

void bluetooth_remove(const gchar *device_path)
{
    GDBusConnection *bus = get_bus();
    if (!bus || !device_path || !adapter_path) return;

    GError   *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync(
        bus, BLUEZ_DBUS_SERVICE, adapter_path,
        BLUEZ_ADAPTER_IFACE, "RemoveDevice",
        g_variant_new("(o)", device_path),
        NULL, G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &err);
    if (ret) g_variant_unref(ret);
    g_clear_error(&err);
}

/* ─────────────────────────────────────────────────────── */
/* Public: bluetooth_get_connected_device                  */
/* ─────────────────────────────────────────────────────── */

BtDevice *bluetooth_get_connected_device(void)
{
    GList *all = build_device_list_sorted();
    BtDevice *found = NULL;
    for (GList *l = all; l; l = l->next) {
        BtDevice *d = l->data;
        if (d->connected && !found) {
            found = d;
            l->data = NULL;  /* take ownership */
        }
    }
    bluetooth_devices_free(all);
    return found;
}
