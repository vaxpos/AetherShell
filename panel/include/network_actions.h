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

/* Trigger an async NM WiFi scan; cb is called when results are ready. */
void network_wifi_scan(WifiScanCallback cb, gpointer user_data);

typedef void (*WifiConnectCallback)(gboolean success, gpointer user_data);

/* Connect to a network.
 *   - If password is NULL and the network is open (or already saved), no
 *     credentials dialog is raised.
 *   - Pass password=NULL for saved networks. */
void network_wifi_connect(const gchar *ssid, const gchar *bssid,
                          const gchar *password, WifiConnectCallback cb, gpointer user_data);

/* Free a list returned via WifiScanCallback */
void network_wifi_networks_free(GList *list);

#endif // NETWORK_ACTIONS_H
