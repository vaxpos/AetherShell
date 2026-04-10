/*
 * network_actions.c
 *
 * Communicates with NetworkManager exclusively via D-Bus (GDBus).
 * No nmcli / g_spawn_* calls anywhere in this file.
 *
 * Covered operations:
 *   1. Query / toggle WiFi radio          (WirelessEnabled property)
 *   2. Query / toggle Ethernet device     (Device.State + ActivateConnection/Disconnect)
 *   3. Scan for available WiFi networks   (Device.Wireless.RequestScan + GetAllAccessPoints)
 *   4. Connect to a WiFi network          (AddAndActivateConnection / ActivateConnection)
 *   5. Poll the active WiFi/Ethernet info (ActiveAccessPoint + Device properties)
 */

#include "network_actions.h"
#include <gio/gio.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* D-Bus constants                                                             */
/* ─────────────────────────────────────────────────────────────────────────── */

#define NM_DBUS_SERVICE          "org.freedesktop.NetworkManager"
#define NM_DBUS_PATH             "/org/freedesktop/NetworkManager"
#define NM_DBUS_IFACE            "org.freedesktop.NetworkManager"
#define NM_DEV_IFACE             "org.freedesktop.NetworkManager.Device"
#define NM_DEV_WIRELESS_IFACE    "org.freedesktop.NetworkManager.Device.Wireless"
#define NM_AP_IFACE              "org.freedesktop.NetworkManager.AccessPoint"
#define NM_CONN_IFACE            "org.freedesktop.NetworkManager.Settings.Connection"
#define NM_SETTINGS_IFACE        "org.freedesktop.NetworkManager.Settings"
#define NM_SETTINGS_PATH         "/org/freedesktop/NetworkManager/Settings"
#define NM_ACTIVE_CONN_IFACE     "org.freedesktop.NetworkManager.Connection.Active"
#define DBUS_PROPS_IFACE         "org.freedesktop.DBus.Properties"

/* NM Device types */
#define NM_DEVICE_TYPE_ETHERNET  1
#define NM_DEVICE_TYPE_WIFI      2

/* NM Device states */
#define NM_DEVICE_STATE_ACTIVATED 100

/* ─────────────────────────────────────────────────────────────────────────── */
/* Helpers: D-Bus property reads / writes                                      */
/* ─────────────────────────────────────────────────────────────────────────── */

static GVariant *get_dbus_property(GDBusConnection *bus,
                                   const gchar *dest,
                                   const gchar *path,
                                   const gchar *iface,
                                   const gchar *prop)
{
    GError   *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync(
        bus, dest, path, DBUS_PROPS_IFACE, "Get",
        g_variant_new("(ss)", iface, prop),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &err);

    if (!ret) { g_clear_error(&err); return NULL; }

    GVariant *inner = NULL;
    g_variant_get(ret, "(v)", &inner);
    g_variant_unref(ret);
    return inner;
}

static gboolean set_dbus_property(GDBusConnection *bus,
                                  const gchar *dest,
                                  const gchar *path,
                                  const gchar *iface,
                                  const gchar *prop,
                                  GVariant    *value)
{
    GError   *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync(
        bus, dest, path, DBUS_PROPS_IFACE, "Set",
        g_variant_new("(ssv)", iface, prop, value),
        NULL,
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &err);

    if (!ret) { g_clear_error(&err); return FALSE; }
    g_variant_unref(ret);
    return TRUE;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Shared system-bus connection (lazily opened, never closed at runtime)       */
/* ─────────────────────────────────────────────────────────────────────────── */

static GDBusConnection *shared_bus(void)
{
    static GDBusConnection *bus = NULL;
    if (!bus) {
        GError *err = NULL;
        bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
        if (!bus) { g_clear_error(&err); }
    }
    return bus;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Device helpers                                                              */
/* ─────────────────────────────────────────────────────────────────────────── */

/* Returns a newly-allocated object path for the first device of the given NM
 * device type, or NULL if none found. Caller must g_free(). */
static gchar *find_device_path_by_type(GDBusConnection *bus, guint32 wanted_type)
{
    GError   *err    = NULL;
    GVariant *devs_v = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, NM_DBUS_PATH, NM_DBUS_IFACE,
        "GetDevices", NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (!devs_v) { g_clear_error(&err); return NULL; }

    GVariantIter *it  = NULL;
    gchar        *dev = NULL;
    gchar        *found = NULL;
    g_variant_get(devs_v, "(ao)", &it);

    while (g_variant_iter_next(it, "o", &dev)) {
        GVariant *type_v = get_dbus_property(bus, NM_DBUS_SERVICE, dev,
                                             NM_DEV_IFACE, "DeviceType");
        if (type_v) {
            guint32 type = g_variant_get_uint32(type_v);
            g_variant_unref(type_v);
            if (type == wanted_type && !found) {
                found = dev;   /* ownership transferred */
                dev   = NULL;
            }
        }
        g_free(dev);
        dev = NULL;
        if (found) break;
    }

    g_variant_iter_free(it);
    g_variant_unref(devs_v);
    return found;
}

/* Find first WiFi device (convenience wrapper) */
static gchar *find_wifi_device_path(GDBusConnection *bus)
{
    return find_device_path_by_type(bus, NM_DEVICE_TYPE_WIFI);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* 1. WiFi toggle — WirelessEnabled property                                   */
/* ─────────────────────────────────────────────────────────────────────────── */

static void (*ui_wifi_cb)(gboolean) = NULL;
static void (*ui_eth_cb)(gboolean)  = NULL;

static gboolean current_wifi_state = FALSE;
static gboolean current_eth_state  = FALSE;

static gboolean check_wifi_state(gpointer user_data)
{
    (void)user_data;
    GDBusConnection *bus = shared_bus();
    if (!bus) { if (ui_wifi_cb) ui_wifi_cb(FALSE); return G_SOURCE_REMOVE; }

    GVariant *v = get_dbus_property(bus, NM_DBUS_SERVICE, NM_DBUS_PATH,
                                    NM_DBUS_IFACE, "WirelessEnabled");
    if (v) {
        current_wifi_state = g_variant_get_boolean(v);
        g_variant_unref(v);
    }
    if (ui_wifi_cb) ui_wifi_cb(current_wifi_state);
    return G_SOURCE_REMOVE;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* 2. Ethernet state query — GetDevices + Device.State                         */
/* ─────────────────────────────────────────────────────────────────────────── */

static gboolean check_eth_state(gpointer user_data)
{
    (void)user_data;
    GDBusConnection *bus = shared_bus();
    if (!bus) { if (ui_eth_cb) ui_eth_cb(FALSE); return G_SOURCE_REMOVE; }

    GError   *err    = NULL;
    GVariant *devs_v = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, NM_DBUS_PATH, NM_DBUS_IFACE,
        "GetDevices", NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    current_eth_state = FALSE;

    if (!devs_v) { g_clear_error(&err); if (ui_eth_cb) ui_eth_cb(FALSE); return G_SOURCE_REMOVE; }

    GVariantIter *it  = NULL;
    gchar        *dev = NULL;
    g_variant_get(devs_v, "(ao)", &it);

    while (g_variant_iter_next(it, "o", &dev)) {
        GVariant *type_v = get_dbus_property(bus, NM_DBUS_SERVICE, dev,
                                             NM_DEV_IFACE, "DeviceType");
        if (type_v) {
            guint32 type = g_variant_get_uint32(type_v);
            g_variant_unref(type_v);
            if (type == NM_DEVICE_TYPE_ETHERNET) {
                GVariant *state_v = get_dbus_property(bus, NM_DBUS_SERVICE, dev,
                                                      NM_DEV_IFACE, "State");
                if (state_v) {
                    guint32 state = g_variant_get_uint32(state_v);
                    g_variant_unref(state_v);
                    if (state == NM_DEVICE_STATE_ACTIVATED) {
                        current_eth_state = TRUE;
                        g_free(dev);
                        break;
                    }
                }
            }
        }
        g_free(dev);
        dev = NULL;
    }

    g_variant_iter_free(it);
    g_variant_unref(devs_v);
    if (ui_eth_cb) ui_eth_cb(current_eth_state);
    return G_SOURCE_REMOVE;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Public init                                                                 */
/* ─────────────────────────────────────────────────────────────────────────── */

void network_init_state(void (*wifi_cb)(gboolean), void (*eth_cb)(gboolean))
{
    ui_wifi_cb = wifi_cb;
    ui_eth_cb  = eth_cb;
    check_wifi_state(NULL);
    check_eth_state(NULL);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* 3. Toggle WiFi — Set WirelessEnabled                                        */
/* ─────────────────────────────────────────────────────────────────────────── */

void network_toggle_wifi(void)
{
    gboolean next = !current_wifi_state;
    /* Optimistic UI update */
    if (ui_wifi_cb) ui_wifi_cb(next);

    GDBusConnection *bus = shared_bus();
    if (!bus) { g_timeout_add_seconds(1, check_wifi_state, NULL); return; }

    set_dbus_property(bus, NM_DBUS_SERVICE, NM_DBUS_PATH, NM_DBUS_IFACE,
                      "WirelessEnabled", g_variant_new_boolean(next));

    /* Re-read actual state after NM processes the request */
    g_timeout_add_seconds(2, check_wifi_state, NULL);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* 4. Toggle Ethernet — ActivateConnection / Device.Disconnect                 */
/* ─────────────────────────────────────────────────────────────────────────── */

void network_toggle_ethernet(void)
{
    gboolean next = !current_eth_state;
    if (ui_eth_cb) ui_eth_cb(next);

    GDBusConnection *bus = shared_bus();
    if (!bus) { g_timeout_add_seconds(1, check_eth_state, NULL); return; }

    GError   *err    = NULL;
    GVariant *devs_v = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, NM_DBUS_PATH, NM_DBUS_IFACE,
        "GetDevices", NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (!devs_v) { g_clear_error(&err); goto done; }

    {
        GVariantIter *it  = NULL;
        gchar        *dev = NULL;
        g_variant_get(devs_v, "(ao)", &it);

        while (g_variant_iter_next(it, "o", &dev)) {
            GVariant *type_v = get_dbus_property(bus, NM_DBUS_SERVICE, dev,
                                                 NM_DEV_IFACE, "DeviceType");
            if (type_v) {
                guint32 type = g_variant_get_uint32(type_v);
                g_variant_unref(type_v);

                if (type == NM_DEVICE_TYPE_ETHERNET) {
                    GError *e2 = NULL;
                    if (next) {
                        /* ActivateConnection("", device_path, "/") — NM picks the best profile */
                        GVariant *r = g_dbus_connection_call_sync(
                            bus, NM_DBUS_SERVICE, NM_DBUS_PATH, NM_DBUS_IFACE,
                            "ActivateConnection",
                            g_variant_new("(ooo)", "/", dev, "/"),
                            G_VARIANT_TYPE("(o)"),
                            G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &e2);
                        if (r) g_variant_unref(r);
                        g_clear_error(&e2);
                    } else {
                        /* Device.Disconnect */
                        GVariant *r = g_dbus_connection_call_sync(
                            bus, NM_DBUS_SERVICE, dev, NM_DEV_IFACE,
                            "Disconnect", NULL, NULL,
                            G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &e2);
                        if (r) g_variant_unref(r);
                        g_clear_error(&e2);
                    }
                }
            }
            g_free(dev);
            dev = NULL;
        }
        g_variant_iter_free(it);
        g_variant_unref(devs_v);
    }

done:
    g_timeout_add_seconds(2, check_eth_state, NULL);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* WiFi scan helpers                                                            */
/* ─────────────────────────────────────────────────────────────────────────── */

void network_wifi_networks_free(GList *list)
{
    for (GList *l = list; l; l = l->next) {
        WifiNetwork *n = l->data;
        g_free(n->ssid);
        g_free(n->bssid);
        g_free(n);
    }
    g_list_free(list);
}

typedef struct {
    WifiScanCallback  cb;
    gpointer          user_data;
    gchar            *wifi_dev_path;
    GDBusConnection  *bus;
} ScanCtx;

/* ── Saved SSIDs (via NM Settings) ─────────────────────────────────────────── */

static GHashTable *get_saved_ssids_dbus(GDBusConnection *bus)
{
    GHashTable *saved = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GError   *err     = NULL;
    GVariant *conns_v = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, NM_SETTINGS_PATH, NM_SETTINGS_IFACE,
        "ListConnections", NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (!conns_v) { g_clear_error(&err); return saved; }

    GVariantIter *it        = NULL;
    gchar        *conn_path = NULL;
    g_variant_get(conns_v, "(ao)", &it);

    while (g_variant_iter_next(it, "o", &conn_path)) {
        GVariant *settings_v = g_dbus_connection_call_sync(
            bus, NM_DBUS_SERVICE, conn_path, NM_CONN_IFACE,
            "GetSettings", NULL, G_VARIANT_TYPE("(a{sa{sv}})"),
            G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL);

        if (settings_v) {
            GVariant *dict = NULL;
            g_variant_get(settings_v, "(@a{sa{sv}})", &dict);
            if (dict) {
                GVariant *wifi_dict = g_variant_lookup_value(dict, "802-11-wireless",
                                                             G_VARIANT_TYPE("a{sv}"));
                if (wifi_dict) {
                    GVariant *ssid_v = g_variant_lookup_value(wifi_dict, "ssid",
                                                              G_VARIANT_TYPE("ay"));
                    if (ssid_v) {
                        gsize len = 0;
                        const guchar *bytes = g_variant_get_fixed_array(ssid_v, &len, 1);
                        if (bytes && len > 0)
                            g_hash_table_add(saved, g_strndup((const gchar *)bytes, len));
                        g_variant_unref(ssid_v);
                    }
                    g_variant_unref(wifi_dict);
                }
                g_variant_unref(dict);
            }
            g_variant_unref(settings_v);
        }
        g_free(conn_path);
        conn_path = NULL;
    }
    g_variant_iter_free(it);
    g_variant_unref(conns_v);
    return saved;
}

/* ── Sort comparator (portable static function) ──────────────────────────── */

static gint wifi_strength_cmp(gconstpointer a, gconstpointer b)
{
    const WifiNetwork *na = a;
    const WifiNetwork *nb = b;
    return (gint)nb->strength - (gint)na->strength;
}

/* ── Collect APs from NM ─────────────────────────────────────────────────── */

static GList *collect_access_points(GDBusConnection *bus, const gchar *dev_path)
{
    GHashTable *saved_ssids = get_saved_ssids_dbus(bus);
    GList  *result = NULL;
    GError *err    = NULL;

    GVariant *aps_v = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, dev_path,
        NM_DEV_WIRELESS_IFACE, "GetAllAccessPoints",
        NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (!aps_v) { g_clear_error(&err); g_hash_table_destroy(saved_ssids); return NULL; }

    /* Active AP path */
    GVariant *active_ap_v  = get_dbus_property(bus, NM_DBUS_SERVICE, dev_path,
                                               NM_DEV_WIRELESS_IFACE, "ActiveAccessPoint");
    gchar *active_ap_path = NULL;
    if (active_ap_v) {
        active_ap_path = g_strdup(g_variant_get_string(active_ap_v, NULL));
        g_variant_unref(active_ap_v);
    }

    GVariantIter *it      = NULL;
    gchar        *ap_path = NULL;
    g_variant_get(aps_v, "(ao)", &it);

    while (g_variant_iter_next(it, "o", &ap_path)) {
        gboolean connected = (active_ap_path &&
                              g_strcmp0(ap_path, active_ap_path) == 0 &&
                              g_strcmp0(active_ap_path, "/") != 0);

        /* SSID */
        GVariant *ssid_v = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path,
                                             NM_AP_IFACE, "Ssid");
        if (!ssid_v) { g_free(ap_path); ap_path = NULL; continue; }

        gsize ssid_len = 0;
        const guchar *ssid_bytes = g_variant_get_fixed_array(ssid_v, &ssid_len, 1);
        gchar *ssid_str = g_strndup((const gchar *)ssid_bytes, ssid_len);
        g_variant_unref(ssid_v);

        /* Skip hidden (empty SSID) */
        if (!ssid_str || ssid_str[0] == '\0') {
            g_free(ssid_str);
            g_free(ap_path); ap_path = NULL;
            continue;
        }

        /* Strength */
        GVariant *str_v  = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path, NM_AP_IFACE, "Strength");
        guchar strength  = str_v ? g_variant_get_byte(str_v) : 0;
        if (str_v) g_variant_unref(str_v);

        /* Security: RsnFlags | WpaFlags */
        GVariant *rsn_v  = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path, NM_AP_IFACE, "RsnFlags");
        GVariant *wpa_v  = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path, NM_AP_IFACE, "WpaFlags");
        guint32 rsn      = rsn_v ? g_variant_get_uint32(rsn_v) : 0;
        guint32 wpa      = wpa_v ? g_variant_get_uint32(wpa_v) : 0;
        if (rsn_v) g_variant_unref(rsn_v);
        if (wpa_v) g_variant_unref(wpa_v);
        gboolean secured = (rsn != 0 || wpa != 0);

        /* BSSID / HwAddress */
        GVariant *hw_v = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path, NM_AP_IFACE, "HwAddress");
        gchar *bssid   = hw_v ? g_strdup(g_variant_get_string(hw_v, NULL)) : g_strdup("");
        if (hw_v) g_variant_unref(hw_v);

        /* Dedup by SSID — keep strongest signal */
        gboolean found = FALSE;
        for (GList *l = result; l; l = l->next) {
            WifiNetwork *ex = l->data;
            if (g_strcmp0(ex->ssid, ssid_str) == 0) {
                if (strength > ex->strength) {
                    ex->strength = strength;
                    g_free(ex->bssid);
                    ex->bssid = bssid;
                    bssid     = NULL;
                } else {
                    g_free(bssid);
                    bssid = NULL;
                }
                if (connected) ex->connected = TRUE;
                found = TRUE;
                break;
            }
        }

        if (!found) {
            WifiNetwork *net = g_new0(WifiNetwork, 1);
            net->ssid      = ssid_str;
            net->bssid     = bssid;
            net->strength  = strength;
            net->secured   = secured;
            net->saved     = g_hash_table_contains(saved_ssids, ssid_str);
            net->connected = connected;
            result = g_list_append(result, net);
        } else {
            g_free(ssid_str);
        }

        g_free(ap_path); ap_path = NULL;
    }

    g_free(active_ap_path);
    g_hash_table_destroy(saved_ssids);
    g_variant_iter_free(it);
    g_variant_unref(aps_v);

    return g_list_sort(result, wifi_strength_cmp);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* 5. WiFi scan (public)                                                        */
/* ─────────────────────────────────────────────────────────────────────────── */

static gboolean on_scan_wait_done(gpointer user_data)
{
    ScanCtx *ctx  = user_data;
    GList   *nets = collect_access_points(ctx->bus, ctx->wifi_dev_path);

    if (ctx->cb) ctx->cb(nets, ctx->user_data);

    g_object_unref(ctx->bus);
    g_free(ctx->wifi_dev_path);
    g_free(ctx);
    return G_SOURCE_REMOVE;
}

void network_wifi_scan(WifiScanCallback cb, gpointer user_data)
{
    GDBusConnection *bus = shared_bus();
    if (!bus) { if (cb) cb(NULL, user_data); return; }

    gchar *wifi_path = find_wifi_device_path(bus);
    if (!wifi_path) { if (cb) cb(NULL, user_data); return; }

    /* RequestScan({}) — fire and forget; NM returns results after ~1-2 s */
    GError   *err      = NULL;
    GVariant *scan_ret = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, wifi_path,
        NM_DEV_WIRELESS_IFACE, "RequestScan",
        g_variant_new("(a{sv})", NULL),
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
    if (scan_ret) g_variant_unref(scan_ret);
    g_clear_error(&err);   /* "scan already in progress" is not fatal */

    /* Return current (pre-scan) AP list immediately */
    GList *current = collect_access_points(bus, wifi_path);
    if (cb) cb(current, user_data);

    /* Return fresh (post-scan) list after 2.2 s */
    ScanCtx *ctx        = g_new0(ScanCtx, 1);
    ctx->cb             = cb;
    ctx->user_data      = user_data;
    ctx->wifi_dev_path  = g_strdup(wifi_path);
    ctx->bus            = g_object_ref(bus);
    g_timeout_add(2200, on_scan_wait_done, ctx);

    g_free(wifi_path);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* 6. WiFi connect — pure D-Bus, no nmcli, no shell injection                  */
/*                                                                             */
/*   Strategy:                                                                 */
/*     a) Look for an existing NM connection profile whose 802-11-wireless     */
/*        ssid matches.  If found → ActivateConnection.                        */
/*     b) Otherwise → AddAndActivateConnection with a minimal settings dict.   */
/* ─────────────────────────────────────────────────────────────────────────── */

typedef struct {
    WifiConnectCallback cb;
    gpointer            user_data;
    GDBusConnection    *bus;
    gchar              *dev_path;
    gchar              *ap_path;
    /* signal subscription id */
    guint               sig_id;
    /* timeout guard */
    guint               timeout_id;
    gchar              *active_conn_path;
} ConnectCtx;

/* Forward declaration */
static void connect_ctx_free(ConnectCtx *ctx);

/* Timeout guard — treat as failure if NM never reaches activated state */
static gboolean on_connect_timeout(gpointer user_data)
{
    ConnectCtx *ctx = user_data;
    ctx->timeout_id = 0;
    if (ctx->sig_id) {
        g_dbus_connection_signal_unsubscribe(ctx->bus, ctx->sig_id);
        ctx->sig_id = 0;
    }
    if (ctx->cb) ctx->cb(FALSE, ctx->user_data);
    connect_ctx_free(ctx);
    return G_SOURCE_REMOVE;
}

static void connect_ctx_free(ConnectCtx *ctx)
{
    if (ctx->timeout_id) { g_source_remove(ctx->timeout_id); ctx->timeout_id = 0; }
    if (ctx->sig_id)     { g_dbus_connection_signal_unsubscribe(ctx->bus, ctx->sig_id); ctx->sig_id = 0; }
    g_object_unref(ctx->bus);
    g_free(ctx->dev_path);
    g_free(ctx->ap_path);
    g_free(ctx->active_conn_path);
    g_free(ctx);
}

/* PropertiesChanged on the ActiveConnection object — watch State */
static void on_active_conn_props_changed(GDBusConnection *conn,
                                         const gchar     *sender,
                                         const gchar     *object_path,
                                         const gchar     *iface_name,
                                         const gchar     *signal_name,
                                         GVariant        *parameters,
                                         gpointer         user_data)
{
    (void)conn; (void)sender; (void)iface_name; (void)signal_name;

    ConnectCtx *ctx = user_data;
    if (g_strcmp0(object_path, ctx->active_conn_path) != 0) return;

    /* parameters: (sa{sv}as) — interface, changed props, invalidated props */
    GVariant *changed = NULL;
    g_variant_get(parameters, "(&s@a{sv}as)", NULL, &changed, NULL);
    if (!changed) return;

    GVariant *state_v = g_variant_lookup_value(changed, "State", G_VARIANT_TYPE_UINT32);
    if (state_v) {
        guint32 state = g_variant_get_uint32(state_v);
        g_variant_unref(state_v);

        /* NM_ACTIVE_CONNECTION_STATE_ACTIVATED = 2, DEACTIVATED = 4 */
        if (state == 2) {
            g_dbus_connection_signal_unsubscribe(ctx->bus, ctx->sig_id);
            ctx->sig_id = 0;
            if (ctx->cb) ctx->cb(TRUE, ctx->user_data);
            connect_ctx_free(ctx);
        } else if (state == 4) {
            g_dbus_connection_signal_unsubscribe(ctx->bus, ctx->sig_id);
            ctx->sig_id = 0;
            if (ctx->cb) ctx->cb(FALSE, ctx->user_data);
            connect_ctx_free(ctx);
        }
    }
    g_variant_unref(changed);
}

/* Subscribe to PropertiesChanged and install timeout guard */
static void watch_active_connection(ConnectCtx *ctx, const gchar *active_path)
{
    ctx->active_conn_path = g_strdup(active_path);

    ctx->sig_id = g_dbus_connection_signal_subscribe(
        ctx->bus,
        NM_DBUS_SERVICE,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        active_path,
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_active_conn_props_changed,
        ctx,
        NULL);

    /* 30-second timeout guard */
    ctx->timeout_id = g_timeout_add_seconds(30, on_connect_timeout, ctx);
}

/* Look up an existing saved connection path for the given SSID (or NULL) */
static gchar *find_saved_conn_for_ssid(GDBusConnection *bus, const gchar *ssid)
{
    GError   *err     = NULL;
    GVariant *conns_v = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, NM_SETTINGS_PATH, NM_SETTINGS_IFACE,
        "ListConnections", NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (!conns_v) { g_clear_error(&err); return NULL; }

    GVariantIter *it        = NULL;
    gchar        *conn_path = NULL;
    gchar        *found     = NULL;
    g_variant_get(conns_v, "(ao)", &it);

    while (!found && g_variant_iter_next(it, "o", &conn_path)) {
        GVariant *sv = g_dbus_connection_call_sync(
            bus, NM_DBUS_SERVICE, conn_path, NM_CONN_IFACE,
            "GetSettings", NULL, G_VARIANT_TYPE("(a{sa{sv}})"),
            G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL);

        if (sv) {
            GVariant *dict = NULL;
            g_variant_get(sv, "(@a{sa{sv}})", &dict);
            if (dict) {
                GVariant *wd = g_variant_lookup_value(dict, "802-11-wireless",
                                                      G_VARIANT_TYPE("a{sv}"));
                if (wd) {
                    GVariant *ssid_v = g_variant_lookup_value(wd, "ssid",
                                                              G_VARIANT_TYPE("ay"));
                    if (ssid_v) {
                        gsize len = 0;
                        const guchar *b = g_variant_get_fixed_array(ssid_v, &len, 1);
                        if (b && len > 0 && strlen(ssid) == len &&
                            memcmp(b, ssid, len) == 0) {
                            found = g_strdup(conn_path);
                        }
                        g_variant_unref(ssid_v);
                    }
                    g_variant_unref(wd);
                }
                g_variant_unref(dict);
            }
            g_variant_unref(sv);
        }
        g_free(conn_path);
        conn_path = NULL;
    }
    g_variant_iter_free(it);
    g_variant_unref(conns_v);
    return found;
}

void network_wifi_connect(const gchar *ssid, const gchar *bssid,
                          const gchar *password,
                          WifiConnectCallback cb, gpointer user_data)
{
    if (!ssid) { if (cb) cb(FALSE, user_data); return; }

    GDBusConnection *bus = shared_bus();
    if (!bus) { if (cb) cb(FALSE, user_data); return; }

    gchar *wifi_path = find_wifi_device_path(bus);
    if (!wifi_path) { if (cb) cb(FALSE, user_data); return; }

    /* Find AP object path that matches the SSID (take the strongest one) */
    GError   *err   = NULL;
    GVariant *aps_v = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, wifi_path,
        NM_DEV_WIRELESS_IFACE, "GetAllAccessPoints",
        NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    gchar  *ap_path    = NULL;
    guchar  best_strength = 0;

    if (aps_v) {
        GVariantIter *it  = NULL;
        gchar        *ap  = NULL;
        g_variant_get(aps_v, "(ao)", &it);
        while (g_variant_iter_next(it, "o", &ap)) {
            GVariant *sv = get_dbus_property(bus, NM_DBUS_SERVICE, ap, NM_AP_IFACE, "Ssid");
            if (sv) {
                gsize len = 0;
                const guchar *bytes = g_variant_get_fixed_array(sv, &len, 1);
                if (bytes && len == strlen(ssid) && memcmp(bytes, ssid, len) == 0) {
                    GVariant *strv = get_dbus_property(bus, NM_DBUS_SERVICE, ap,
                                                       NM_AP_IFACE, "Strength");
                    guchar strength = strv ? g_variant_get_byte(strv) : 0;
                    if (strv) g_variant_unref(strv);
                    if (!ap_path || strength > best_strength) {
                        g_free(ap_path);
                        ap_path       = g_strdup(ap);
                        best_strength = strength;
                    }
                }
                g_variant_unref(sv);
            }
            g_free(ap);
            ap = NULL;
        }
        g_variant_iter_free(it);
        g_variant_unref(aps_v);
    }
    g_clear_error(&err);

    if (!ap_path) ap_path = g_strdup("/");   /* NM will pick one */

    /* Try to find an existing saved profile for this SSID */
    gchar *saved_conn = find_saved_conn_for_ssid(bus, ssid);

    ConnectCtx *ctx     = g_new0(ConnectCtx, 1);
    ctx->cb             = cb;
    ctx->user_data      = user_data;
    ctx->bus            = g_object_ref(bus);
    ctx->dev_path       = g_strdup(wifi_path);
    ctx->ap_path        = g_strdup(ap_path);

    if (saved_conn && !(password && *password)) {
        /* ── Case A: known profile, no new password → ActivateConnection ── */
        GError   *e2  = NULL;
        GVariant *ret = g_dbus_connection_call_sync(
            bus, NM_DBUS_SERVICE, NM_DBUS_PATH, NM_DBUS_IFACE,
            "ActivateConnection",
            g_variant_new("(ooo)", saved_conn, wifi_path, ap_path),
            G_VARIANT_TYPE("(o)"),
            G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &e2);

        if (ret) {
            gchar *active_path = NULL;
            g_variant_get(ret, "(o)", &active_path);
            watch_active_connection(ctx, active_path);
            g_free(active_path);
            g_variant_unref(ret);
        } else {
            g_clear_error(&e2);
            if (cb) cb(FALSE, user_data);
            connect_ctx_free(ctx);
        }
    } else {
        /* ── Case B: new/open network or new password → AddAndActivateConnection ── */
        GVariantBuilder conn_builder;
        gchar *uuid = g_uuid_string_random();
        g_variant_builder_init(&conn_builder, G_VARIANT_TYPE("a{sa{sv}}"));

        /* connection section */
        {
            GVariantBuilder s;
            g_variant_builder_init(&s, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&s, "{sv}", "id",
                                  g_variant_new_string(ssid));
            g_variant_builder_add(&s, "{sv}", "uuid",
                                  g_variant_new_string(uuid));
            g_variant_builder_add(&s, "{sv}", "type",
                                  g_variant_new_string("802-11-wireless"));
            g_variant_builder_add(&conn_builder, "{sa{sv}}", "connection", &s);
        }

        /* 802-11-wireless section */
        {
            GVariantBuilder s;
            g_variant_builder_init(&s, G_VARIANT_TYPE("a{sv}"));
            GVariant *ssid_bytes = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                       (const guchar *)ssid, strlen(ssid), 1);
            g_variant_builder_add(&s, "{sv}", "ssid", ssid_bytes);
            g_variant_builder_add(&s, "{sv}", "mode", g_variant_new_string("infrastructure"));
            g_variant_builder_add(&conn_builder, "{sa{sv}}", "802-11-wireless", &s);
        }

        /* 802-11-wireless-security section (only if password provided) */
        if (password && *password) {
            GVariantBuilder s;
            g_variant_builder_init(&s, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&s, "{sv}", "key-mgmt",
                                  g_variant_new_string("wpa-psk"));
            g_variant_builder_add(&s, "{sv}", "psk",
                                  g_variant_new_string(password));
            g_variant_builder_add(&conn_builder, "{sa{sv}}",
                                  "802-11-wireless-security", &s);
        }

        /* ipv4 + ipv6 — auto */
        {
            GVariantBuilder s;
            g_variant_builder_init(&s, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&s, "{sv}", "method", g_variant_new_string("auto"));
            g_variant_builder_add(&conn_builder, "{sa{sv}}", "ipv4", &s);
        }
        {
            GVariantBuilder s;
            g_variant_builder_init(&s, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&s, "{sv}", "method", g_variant_new_string("auto"));
            g_variant_builder_add(&conn_builder, "{sa{sv}}", "ipv6", &s);
        }

        GError   *e2  = NULL;
        GVariant *ret = g_dbus_connection_call_sync(
            bus, NM_DBUS_SERVICE, NM_DBUS_PATH, NM_DBUS_IFACE,
            "AddAndActivateConnection",
            g_variant_new("(a{sa{sv}}oo)",
                          &conn_builder, wifi_path, ap_path),
            G_VARIANT_TYPE("(oo)"),
            G_DBUS_CALL_FLAGS_NONE, 15000, NULL, &e2);

        if (ret) {
            gchar *conn_obj   = NULL;
            gchar *active_obj = NULL;
            g_variant_get(ret, "(oo)", &conn_obj, &active_obj);
            watch_active_connection(ctx, active_obj);
            g_free(conn_obj);
            g_free(active_obj);
            g_variant_unref(ret);
        } else {
            g_clear_error(&e2);
            if (cb) cb(FALSE, user_data);
            connect_ctx_free(ctx);
        }

        g_free(uuid);
    }

    g_free(saved_conn);
    g_free(ap_path);
    g_free(wifi_path);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* 7. Active WiFi/Ethernet polling — fully D-Bus, no nmcli                     */
/* ─────────────────────────────────────────────────────────────────────────── */

static WifiActiveInfoCallback active_wifi_cb        = NULL;
static gpointer               active_wifi_user_data = NULL;

/* Read active Ethernet info via D-Bus.
 * Returns TRUE and fills *info if an ethernet device is activated. */
static gboolean get_active_ethernet_info_dbus(GDBusConnection *bus,
                                              WifiActiveInfo  *info)
{
    if (!info) return FALSE;

    GError   *err    = NULL;
    GVariant *devs_v = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, NM_DBUS_PATH, NM_DBUS_IFACE,
        "GetDevices", NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (!devs_v) { g_clear_error(&err); return FALSE; }

    gboolean found = FALSE;
    GVariantIter *it  = NULL;
    gchar        *dev = NULL;
    g_variant_get(devs_v, "(ao)", &it);

    while (!found && g_variant_iter_next(it, "o", &dev)) {
        GVariant *type_v = get_dbus_property(bus, NM_DBUS_SERVICE, dev,
                                             NM_DEV_IFACE, "DeviceType");
        if (type_v) {
            guint32 type = g_variant_get_uint32(type_v);
            g_variant_unref(type_v);

            if (type == NM_DEVICE_TYPE_ETHERNET) {
                GVariant *state_v = get_dbus_property(bus, NM_DBUS_SERVICE, dev,
                                                      NM_DEV_IFACE, "State");
                if (state_v) {
                    guint32 state = g_variant_get_uint32(state_v);
                    g_variant_unref(state_v);

                    if (state == NM_DEVICE_STATE_ACTIVATED) {
                        /* Interface name */
                        GVariant *iface_v = get_dbus_property(bus, NM_DBUS_SERVICE, dev,
                                                              NM_DEV_IFACE, "Interface");
                        /* Active connection → connection id */
                        GVariant *ac_v = get_dbus_property(bus, NM_DBUS_SERVICE, dev,
                                                           NM_DEV_IFACE, "ActiveConnection");
                        gchar *conn_id = NULL;
                        if (ac_v) {
                            const gchar *ac_path = g_variant_get_string(ac_v, NULL);
                            if (ac_path && g_strcmp0(ac_path, "/") != 0) {
                                GVariant *id_v = get_dbus_property(bus, NM_DBUS_SERVICE,
                                                                   ac_path,
                                                                   NM_ACTIVE_CONN_IFACE, "Id");
                                if (id_v) {
                                    conn_id = g_strdup(g_variant_get_string(id_v, NULL));
                                    g_variant_unref(id_v);
                                }
                            }
                            g_variant_unref(ac_v);
                        }

                        info->is_ethernet     = TRUE;
                        info->ssid            = g_strdup(conn_id ? conn_id : "Ethernet Connected");
                        info->device          = iface_v ? g_strdup(g_variant_get_string(iface_v, NULL)) : g_strdup("");
                        info->connection_name = g_strdup(conn_id ? conn_id : "Wired");
                        info->strength        = 0;
                        info->frequency       = 0;
                        info->is_5ghz         = FALSE;

                        if (iface_v) g_variant_unref(iface_v);
                        g_free(conn_id);
                        found = TRUE;
                    }
                }
            }
        }
        g_free(dev); dev = NULL;
    }
    g_variant_iter_free(it);
    g_variant_unref(devs_v);
    return found;
}

static gboolean poll_active_wifi(gpointer user_data)
{
    (void)user_data;
    GDBusConnection *bus = shared_bus();
    if (!bus) return G_SOURCE_CONTINUE;

    /* We need the WiFi device path each poll (device could change) */
    gchar *wifi_dev = find_wifi_device_path(bus);
    if (!wifi_dev) {
        /* No WiFi device — try Ethernet */
        WifiActiveInfo eth_info = {0};
        if (get_active_ethernet_info_dbus(bus, &eth_info)) {
            if (active_wifi_cb) active_wifi_cb(&eth_info, active_wifi_user_data);
            g_free(eth_info.ssid);
            g_free(eth_info.device);
            g_free(eth_info.connection_name);
        } else if (active_wifi_cb) {
            active_wifi_cb(NULL, active_wifi_user_data);
        }
        return G_SOURCE_CONTINUE;
    }

    GVariant *active_ap_v = get_dbus_property(bus, NM_DBUS_SERVICE, wifi_dev,
                                              NM_DEV_WIRELESS_IFACE, "ActiveAccessPoint");
    if (!active_ap_v) {
        g_free(wifi_dev);
        WifiActiveInfo eth_info = {0};
        if (get_active_ethernet_info_dbus(bus, &eth_info)) {
            if (active_wifi_cb) active_wifi_cb(&eth_info, active_wifi_user_data);
            g_free(eth_info.ssid);
            g_free(eth_info.device);
            g_free(eth_info.connection_name);
        } else if (active_wifi_cb) {
            active_wifi_cb(NULL, active_wifi_user_data);
        }
        return G_SOURCE_CONTINUE;
    }

    gchar *ap_path = g_strdup(g_variant_get_string(active_ap_v, NULL));
    g_variant_unref(active_ap_v);

    if (g_strcmp0(ap_path, "/") == 0) {
        g_free(ap_path);
        g_free(wifi_dev);
        WifiActiveInfo eth_info = {0};
        if (get_active_ethernet_info_dbus(bus, &eth_info)) {
            if (active_wifi_cb) active_wifi_cb(&eth_info, active_wifi_user_data);
            g_free(eth_info.ssid);
            g_free(eth_info.device);
            g_free(eth_info.connection_name);
        } else if (active_wifi_cb) {
            active_wifi_cb(NULL, active_wifi_user_data);
        }
        return G_SOURCE_CONTINUE;
    }

    /* Read AP properties */
    GVariant *ssid_v = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path, NM_AP_IFACE, "Ssid");
    GVariant *str_v  = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path, NM_AP_IFACE, "Strength");
    GVariant *freq_v = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path, NM_AP_IFACE, "Frequency");

    WifiActiveInfo info = {0};

    if (ssid_v) {
        gsize len = 0;
        const guchar *bytes = g_variant_get_fixed_array(ssid_v, &len, 1);
        info.ssid = g_strndup((const gchar *)bytes, len);
        g_variant_unref(ssid_v);
    }
    if (str_v) {
        info.strength = g_variant_get_byte(str_v);
        g_variant_unref(str_v);
    }
    if (freq_v) {
        info.frequency = g_variant_get_uint32(freq_v);
        info.is_5ghz   = (info.frequency >= 4000);
        g_variant_unref(freq_v);
    }

    if (active_wifi_cb) active_wifi_cb(&info, active_wifi_user_data);

    g_free(info.ssid);
    g_free(ap_path);
    g_free(wifi_dev);
    return G_SOURCE_CONTINUE;
}

void network_watch_active_wifi(WifiActiveInfoCallback cb, gpointer user_data)
{
    active_wifi_cb        = cb;
    active_wifi_user_data = user_data;

    /* Initial poll immediately, then every 3 s */
    poll_active_wifi(NULL);
    g_timeout_add_seconds(3, poll_active_wifi, NULL);
}
