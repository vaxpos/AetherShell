#ifndef NETWORK_ACTIONS_H
#define NETWORK_ACTIONS_H

#include <glib.h>

/* ── Existing toggle API ─────────────────────────────────────────────────── */

void network_init_state(void (*wifi_cb)(gboolean), void (*eth_cb)(gboolean));
void network_toggle_wifi(void);
void network_toggle_ethernet(void);

/* ── WiFi scan & connect (D-Bus NetworkManager) ──────────────────────────── */

typedef struct {
    gchar   *ssid;       /* human-readable SSID string   */
    gchar   *bssid;      /* hardware address             */
    guchar   strength;   /* 0-100                        */
    gboolean secured;    /* TRUE if any security flags   */
    gboolean saved;      /* TRUE if NM has a profile     */
    gboolean connected;  /* TRUE if currently active AP  */
} WifiNetwork;

/* cb is dispatched on the GLib main loop after a scan completes.
 * The caller owns the GList and must call network_wifi_networks_free(). */
typedef void (*WifiScanCallback)(GList *networks, gpointer user_data);

/* Trigger an async NM WiFi scan; cb is called twice:
 *   - immediately with the last cached AP list
 *   - ~2 s later with the freshly scanned list */
void network_wifi_scan(WifiScanCallback cb, gpointer user_data);

typedef void (*WifiConnectCallback)(gboolean success, gpointer user_data);

/* Connect to a WiFi network via D-Bus (AddAndActivateConnection /
 * ActivateConnection).  cb is called with success=TRUE once NM reports
 * NM_ACTIVE_CONNECTION_STATE_ACTIVATED, or FALSE on failure/timeout.
 *   - password may be NULL for open or already-saved networks.
 *   - bssid  may be NULL; NM will pick the best AP automatically. */
void network_wifi_connect(const gchar *ssid, const gchar *bssid,
                          const gchar *password,
                          WifiConnectCallback cb, gpointer user_data);

/* Free a list returned via WifiScanCallback */
void network_wifi_networks_free(GList *list);

/* ── Active WiFi / Ethernet Poller ──────────────────────────────────────── */

typedef struct {
    gchar   *ssid;            /* SSID (WiFi) or connection name (Ethernet) */
    guchar   strength;        /* 0-100 (WiFi only; 0 for Ethernet)         */
    guint32  frequency;       /* MHz  (WiFi only; 0 for Ethernet)          */
    gboolean is_5ghz;         /* TRUE if frequency >= 4000 MHz             */
    gboolean is_ethernet;     /* TRUE when the active link is wired        */
    gchar   *device;          /* kernel interface name (e.g. "eth0")       */
    gchar   *connection_name; /* NM connection id                          */
} WifiActiveInfo;

typedef void (*WifiActiveInfoCallback)(WifiActiveInfo *info, gpointer user_data);

/* Register a callback that is polled every 3 s for the active connection.
 * info is NULL when there is no active WiFi or Ethernet connection. */
void network_watch_active_wifi(WifiActiveInfoCallback cb, gpointer user_data);

#endif /* NETWORK_ACTIONS_H */
