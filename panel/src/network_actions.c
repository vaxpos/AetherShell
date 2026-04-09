/*
 * network_actions.c
 *
 * Communicates with NetworkManager via D-Bus (GDBus) to:
 *   1. Toggle WiFi / Ethernet on/off          (existing behaviour)
 *   2. Scan for available WiFi networks        (new)
 *   3. Connect to a WiFi network               (new)
 */

#include "network_actions.h"
#include <gio/gio.h>
#include <string.h>
#include <sys/wait.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* D-Bus constants                                                             */
/* ─────────────────────────────────────────────────────────────────────────── */

#define NM_DBUS_SERVICE      "org.freedesktop.NetworkManager"
#define NM_DBUS_PATH         "/org/freedesktop/NetworkManager"
#define NM_DBUS_IFACE        "org.freedesktop.NetworkManager"
#define NM_DEV_IFACE         "org.freedesktop.NetworkManager.Device"
#define NM_DEV_WIRELESS_IFACE "org.freedesktop.NetworkManager.Device.Wireless"
#define NM_AP_IFACE          "org.freedesktop.NetworkManager.AccessPoint"
#define NM_CONN_IFACE        "org.freedesktop.NetworkManager.Settings.Connection"
#define NM_SETTINGS_IFACE    "org.freedesktop.NetworkManager.Settings"
#define NM_SETTINGS_PATH     "/org/freedesktop/NetworkManager/Settings"
#define DBUS_PROPS_IFACE     "org.freedesktop.DBus.Properties"

/* NM Device types */
#define NM_DEVICE_TYPE_WIFI  2

/* AP security flags */
#define NM_802_11_AP_SEC_NONE 0x0

/* ─────────────────────────────────────────────────────────────────────────── */
/* Legacy toggle state                                                         */
/* ─────────────────────────────────────────────────────────────────────────── */

static void (*ui_wifi_cb)(gboolean) = NULL;
static void (*ui_eth_cb)(gboolean)  = NULL;

static gboolean current_wifi_state = FALSE;
static gboolean current_eth_state  = FALSE;

static gboolean check_wifi_state(gpointer user_data)
{
    (void)user_data;
    gchar *std_out = NULL;
    GError *error  = NULL;

    if (g_spawn_command_line_sync("nmcli radio wifi", &std_out, NULL, NULL, &error)) {
        current_wifi_state = std_out && g_str_has_prefix(std_out, "enabled");
        g_free(std_out);
    } else {
        g_clear_error(&error);
    }
    if (ui_wifi_cb) ui_wifi_cb(current_wifi_state);
    return G_SOURCE_REMOVE;
}

static gboolean check_eth_state(gpointer user_data)
{
    (void)user_data;
    gchar  *std_out = NULL;
    GError *error   = NULL;

    if (g_spawn_command_line_sync("nmcli -t -f TYPE,STATE dev", &std_out, NULL, NULL, &error)) {
        current_eth_state = FALSE;
        if (std_out) {
            gchar **lines = g_strsplit(std_out, "\n", -1);
            for (int i = 0; lines[i]; i++) {
                if (g_str_has_prefix(lines[i], "ethernet:connected")) {
                    current_eth_state = TRUE;
                    break;
                }
            }
            g_strfreev(lines);
        }
        g_free(std_out);
    } else {
        g_clear_error(&error);
    }
    if (ui_eth_cb) ui_eth_cb(current_eth_state);
    return G_SOURCE_REMOVE;
}

void network_init_state(void (*wifi_cb)(gboolean), void (*eth_cb)(gboolean))
{
    ui_wifi_cb = wifi_cb;
    ui_eth_cb  = eth_cb;
    check_wifi_state(NULL);
    check_eth_state(NULL);
}

void network_toggle_wifi(void)
{
    gboolean next = !current_wifi_state;
    if (ui_wifi_cb) ui_wifi_cb(next);
    g_spawn_command_line_async(next ? "nmcli radio wifi on" : "nmcli radio wifi off", NULL);
    g_timeout_add_seconds(2, check_wifi_state, NULL);
}

void network_toggle_ethernet(void)
{
    gboolean next = !current_eth_state;
    if (ui_eth_cb) ui_eth_cb(next);

    gchar *std_out = NULL;
    if (g_spawn_command_line_sync("nmcli -t -f DEVICE,TYPE dev", &std_out, NULL, NULL, NULL)) {
        if (std_out) {
            gchar **lines = g_strsplit(std_out, "\n", -1);
            for (int i = 0; lines[i]; i++) {
                gchar **parts = g_strsplit(lines[i], ":", -1);
                if (parts && parts[0] && parts[1] && g_strcmp0(parts[1], "ethernet") == 0) {
                    gchar *cmd = g_strdup_printf("nmcli device %s %s",
                        next ? "connect" : "disconnect", parts[0]);
                    g_spawn_command_line_async(cmd, NULL);
                    g_free(cmd);
                }
                g_strfreev(parts);
            }
            g_strfreev(lines);
        }
        g_free(std_out);
    }
    g_timeout_add_seconds(2, check_eth_state, NULL);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Helpers: D-Bus property reads                                               */
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

/* ─────────────────────────────────────────────────────────────────────────── */
/* WiFi scan                                                                   */
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

static GHashTable* get_saved_ssids_dbus(GDBusConnection *bus)
{
    GHashTable *saved = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GError *err = NULL;
    GVariant *conns_v = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, NM_SETTINGS_PATH, NM_SETTINGS_IFACE,
        "ListConnections", NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (!conns_v) { g_clear_error(&err); return saved; }

    GVariantIter *it = NULL;
    gchar *conn_path = NULL;
    g_variant_get(conns_v, "(ao)", &it);

    while (g_variant_iter_next(it, "o", &conn_path)) {
        GVariant *settings_v = g_dbus_connection_call_sync(
            bus, NM_DBUS_SERVICE, conn_path, NM_CONN_IFACE,
            "GetSettings", NULL, G_VARIANT_TYPE("(a{sa{sv}})"),
            G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);

        if (settings_v) {
            GVariant *dict = NULL;
            g_variant_get(settings_v, "(@a{sa{sv}})", &dict);
            if (dict) {
                GVariant *wifi_dict = g_variant_lookup_value(dict, "802-11-wireless", G_VARIANT_TYPE("a{sv}"));
                if (wifi_dict) {
                    GVariant *ssid_v = g_variant_lookup_value(wifi_dict, "ssid", G_VARIANT_TYPE("ay"));
                    if (ssid_v) {
                        gsize len = 0;
                        const guchar *bytes = g_variant_get_fixed_array(ssid_v, &len, 1);
                        if (bytes && len > 0) {
                            g_hash_table_add(saved, g_strndup((const gchar *)bytes, len));
                        }
                        g_variant_unref(ssid_v);
                    }
                    g_variant_unref(wifi_dict);
                }
                g_variant_unref(dict);
            }
            g_variant_unref(settings_v);
        }
        g_free(conn_path);
    }
    g_variant_iter_free(it);
    g_variant_unref(conns_v);
    return saved;
}

static GList *collect_access_points(GDBusConnection *bus, const gchar *dev_path)
{
    GHashTable *saved_ssids = get_saved_ssids_dbus(bus);
    GList  *result = NULL;
    GError *err    = NULL;

    /* GetAllAccessPoints */
    GVariant *aps_v = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, dev_path,
        NM_DEV_WIRELESS_IFACE, "GetAllAccessPoints",
        NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (!aps_v) { g_clear_error(&err); g_hash_table_destroy(saved_ssids); return NULL; }

    /* Active connection (if any) */
    GVariant *active_ap_v = get_dbus_property(bus, NM_DBUS_SERVICE, dev_path, NM_DEV_WIRELESS_IFACE, "ActiveAccessPoint");
    gchar *active_ap_path = NULL;
    if (active_ap_v) {
        active_ap_path = g_strdup(g_variant_get_string(active_ap_v, NULL));
        g_variant_unref(active_ap_v);
    }

    GVariantIter *it = NULL;
    g_variant_get(aps_v, "(ao)", &it);
    gchar *ap_path = NULL;

    while (g_variant_iter_next(it, "o", &ap_path)) {
        gboolean connected = FALSE;
        if (active_ap_path && g_strcmp0(ap_path, active_ap_path) == 0 && g_strcmp0(active_ap_path, "/") != 0) {
            connected = TRUE;
        }
        /* Read SSID */
        GVariant *ssid_v = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path,
                                             NM_AP_IFACE, "Ssid");
        if (!ssid_v) { g_free(ap_path); continue; }

        gsize ssid_len = 0;
        const guchar *ssid_bytes = g_variant_get_fixed_array(ssid_v, &ssid_len, 1);
        gchar *ssid_str = g_strndup((const gchar *)ssid_bytes, ssid_len);
        g_variant_unref(ssid_v);

        /* Skip hidden networks (empty SSID) */
        if (!ssid_str || ssid_str[0] == '\0') {
            g_free(ssid_str);
            g_free(ap_path);
            continue;
        }

        /* Strength */
        GVariant *str_v = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path,
                                            NM_AP_IFACE, "Strength");
        guchar strength = str_v ? g_variant_get_byte(str_v) : 0;
        if (str_v) g_variant_unref(str_v);

        /* Security flags (RsnFlags | WpaFlags) */
        GVariant *rsn_v = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path,
                                            NM_AP_IFACE, "RsnFlags");
        GVariant *wpa_v = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path,
                                            NM_AP_IFACE, "WpaFlags");
        guint32 rsn = rsn_v ? g_variant_get_uint32(rsn_v) : 0;
        guint32 wpa = wpa_v ? g_variant_get_uint32(wpa_v) : 0;
        if (rsn_v) g_variant_unref(rsn_v);
        if (wpa_v) g_variant_unref(wpa_v);
        gboolean secured = (rsn != 0 || wpa != 0);

        /* HwAddress (BSSID) */
        GVariant *hw_v = get_dbus_property(bus, NM_DBUS_SERVICE, ap_path,
                                           NM_AP_IFACE, "HwAddress");
        gchar *bssid = hw_v ? g_strdup(g_variant_get_string(hw_v, NULL)) : g_strdup("");
        if (hw_v) g_variant_unref(hw_v);

        /* Dedup by SSID (keep strongest signal) */
        gboolean found = FALSE;
        for (GList *l = result; l; l = l->next) {
            WifiNetwork *ex = l->data;
            if (g_strcmp0(ex->ssid, ssid_str) == 0) {
                if (strength > ex->strength) {
                    ex->strength = strength;
                    g_free(ex->bssid);
                    ex->bssid = bssid;
                } else {
                    g_free(bssid);
                }
                if (connected) ex->connected = TRUE;
                found = TRUE;
                break;
            }
        }

        if (!found) {
            WifiNetwork *net = g_new0(WifiNetwork, 1);
            net->ssid     = ssid_str;
            net->bssid    = bssid;
            net->strength = strength;
            net->secured  = secured;
            net->saved    = g_hash_table_contains(saved_ssids, ssid_str);
            net->connected = connected;
            result = g_list_append(result, net);
        } else {
            g_free(ssid_str);
        }

        g_free(ap_path);
    }

    g_free(active_ap_path);
    g_hash_table_destroy(saved_ssids);
    g_variant_iter_free(it);
    g_variant_unref(aps_v);

    /* Sort by signal strength descending */
    result = g_list_sort(result, (GCompareFunc)({
        int _cmp(gconstpointer a, gconstpointer b) {
            const WifiNetwork *na = a, *nb = b;
            return (int)nb->strength - (int)na->strength;
        }
        _cmp;
    }));

    return result;
}

/* Called 2 s after RequestScan to give NM time to complete the scan */
static gboolean on_scan_wait_done(gpointer user_data)
{
    ScanCtx *ctx = user_data;
    GList   *nets = collect_access_points(ctx->bus, ctx->wifi_dev_path);

    if (ctx->cb) ctx->cb(nets, ctx->user_data);

    g_object_unref(ctx->bus);
    g_free(ctx->wifi_dev_path);
    g_free(ctx);
    return G_SOURCE_REMOVE;
}

/* Find the first WiFi device object path */
static gchar *find_wifi_device_path(GDBusConnection *bus)
{
    GError   *err     = NULL;
    GVariant *devs_v  = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, NM_DBUS_PATH, NM_DBUS_IFACE,
        "GetDevices", NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (!devs_v) { g_clear_error(&err); return NULL; }

    GVariantIter *it  = NULL;
    gchar        *dev = NULL;
    gchar        *wifi_path = NULL;
    g_variant_get(devs_v, "(ao)", &it);

    while (g_variant_iter_next(it, "o", &dev)) {
        GVariant *type_v = get_dbus_property(bus, NM_DBUS_SERVICE, dev,
                                             NM_DEV_IFACE, "DeviceType");
        if (type_v) {
            guint32 type = g_variant_get_uint32(type_v);
            g_variant_unref(type_v);
            if (type == NM_DEVICE_TYPE_WIFI) {
                wifi_path = g_strdup(dev);
                g_free(dev);
                break;
            }
        }
        g_free(dev);
    }

    g_variant_iter_free(it);
    g_variant_unref(devs_v);
    return wifi_path;
}

void network_wifi_scan(WifiScanCallback cb, gpointer user_data)
{
    GError          *err  = NULL;
    GDBusConnection *bus  = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!bus) { g_clear_error(&err); if (cb) cb(NULL, user_data); return; }

    gchar *wifi_path = find_wifi_device_path(bus);
    if (!wifi_path) {
        g_object_unref(bus);
        if (cb) cb(NULL, user_data);
        return;
    }

    /* RequestScan({}) — fire and forget; results appear after ~1-2 s */
    GVariant *scan_ret = g_dbus_connection_call_sync(
        bus, NM_DBUS_SERVICE, wifi_path,
        NM_DEV_WIRELESS_IFACE, "RequestScan",
        g_variant_new("(a{sv})", NULL),
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
    if (scan_ret) g_variant_unref(scan_ret);
    g_clear_error(&err);   /* ignore "scan already in progress" errors */

    /* Return current AP list immediately (from last scan) */
    GList *current = collect_access_points(bus, wifi_path);
    if (cb) cb(current, user_data);

    /* After 2 s, return fresh results (post-scan) */
    ScanCtx *ctx = g_new0(ScanCtx, 1);
    ctx->cb           = cb;
    ctx->user_data    = user_data;
    ctx->wifi_dev_path = g_strdup(wifi_path);
    ctx->bus          = g_object_ref(bus);
    g_timeout_add(2200, on_scan_wait_done, ctx);

    g_free(wifi_path);
    g_object_unref(bus);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* WiFi connect                                                                */
/* ─────────────────────────────────────────────────────────────────────────── */

typedef struct {
    WifiConnectCallback cb;
    gpointer user_data;
} ConnectWaitData;

static void on_child_exit(GPid pid, gint status, gpointer user_data) {
    ConnectWaitData *data = user_data;
    g_spawn_close_pid(pid);
    gboolean success = FALSE;
#ifdef WIFEXITED
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        success = TRUE;
    }
#else
    if (status == 0) success = TRUE;
#endif
    if (data->cb) {
        data->cb(success, data->user_data);
    }
    g_free(data);
}

void network_wifi_connect(const gchar *ssid, const gchar *bssid, const gchar *password, WifiConnectCallback cb, gpointer user_data)
{
    if (!ssid) return;

    gchar *cmd = NULL;
    if (password && *password) {
        cmd = g_strdup_printf("nmcli device wifi connect \"%s\" password \"%s\"", ssid, password);
    } else {
        cmd = g_strdup_printf("nmcli device wifi connect \"%s\"", ssid);
    }

    gchar **argv;
    GError *err = NULL;

    if (g_shell_parse_argv(cmd, NULL, &argv, &err)) {
        GPid pid;
        if (g_spawn_async(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH, NULL, NULL, &pid, &err)) {
            ConnectWaitData *wait_data = g_new0(ConnectWaitData, 1);
            wait_data->cb = cb;
            wait_data->user_data = user_data;
            g_child_watch_add(pid, on_child_exit, wait_data);
        } else {
            if (cb) cb(FALSE, user_data);
            if (err) g_error_free(err);
        }
        g_strfreev(argv);
    } else {
        if (cb) cb(FALSE, user_data);
        if (err) g_error_free(err);
    }

    g_free(cmd);
}
