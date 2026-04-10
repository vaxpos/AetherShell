#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <glib.h>

/* ── BlueZ D-Bus constants ──────────────────────────────────────────────── */
#define BLUEZ_DBUS_SERVICE      "org.bluez"
#define BLUEZ_DBUS_PATH         "/org/bluez"
#define BLUEZ_ADAPTER_IFACE     "org.bluez.Adapter1"
#define BLUEZ_DEVICE_IFACE      "org.bluez.Device1"
#define DBUS_OBJMANAGER_IFACE   "org.freedesktop.DBus.ObjectManager"
#define DBUS_PROPS_IFACE        "org.freedesktop.DBus.Properties"

/* ── Data structures ────────────────────────────────────────────────────── */

typedef struct {
    gchar   *name;         /* Display name (or address if no name)  */
    gchar   *address;      /* MAC address: "XX:XX:XX:XX:XX:XX"      */
    gchar   *object_path;  /* D-Bus object path                     */
    gchar   *icon;         /* BlueZ icon hint (e.g. "audio-headset")*/
    gint16   rssi;         /* Signal strength in dBm (0 = unknown)  */
    gboolean paired;       /* TRUE if paired with this adapter      */
    gboolean connected;    /* TRUE if currently connected           */
    gboolean trusted;      /* TRUE if trusted                       */
} BtDevice;

/* ── Callback types ─────────────────────────────────────────────────────── */

/* Called when the adapter Powered state changes, or on init */
typedef void (*BtStateCallback)(gboolean powered, gpointer user_data);

/* Called with a fresh GList of BtDevice* after a scan update.
 * Caller must call bluetooth_devices_free() on the list. */
typedef void (*BtDeviceListCallback)(GList *devices, gpointer user_data);

/* Called after a connect / pair attempt */
typedef void (*BtConnectCallback)(gboolean success, gpointer user_data);

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * bluetooth_init:
 * Initialise BlueZ D-Bus connection.  cb is called immediately with the
 * current Powered state and again whenever it changes.
 */
void bluetooth_init(BtStateCallback cb, gpointer user_data);

/** Toggle adapter Powered property on/off. */
void bluetooth_toggle(void);

/**
 * bluetooth_scan:
 * Start a 10-second discovery pass on the adapter.
 * cb is called once immediately with the current known device list, then
 * again whenever a new device appears or an existing one changes state.
 * Calling this again during a scan is safe (extends the window).
 */
void bluetooth_scan(BtDeviceListCallback cb, gpointer user_data);

/** Stop an in-progress scan early. */
void bluetooth_scan_stop(void);

/**
 * bluetooth_connect:
 * Connect to a paired device.  If the device is not yet paired,
 * bluetooth_pair() is called first automatically.
 * cb(TRUE, …) is called on NM_ACTIVE_CONNECTION_STATE_ACTIVATED equivalent.
 */
void bluetooth_connect(const gchar *device_path,
                       BtConnectCallback cb, gpointer user_data);

/** Disconnect a connected device.  Non-blocking. */
void bluetooth_disconnect(const gchar *device_path);

/**
 * bluetooth_pair:
 * Initiate pairing.  cb is called when Paired property becomes TRUE
 * or after a 30-second timeout with success=FALSE.
 */
void bluetooth_pair(const gchar *device_path,
                    BtConnectCallback cb, gpointer user_data);

/** Remove (forget) a device from the adapter. */
void bluetooth_remove(const gchar *device_path);

/** Free a list returned via BtDeviceListCallback. */
void bluetooth_devices_free(GList *list);

/**
 * bluetooth_get_connected_device:
 * Returns a newly-allocated BtDevice* for the first currently-connected
 * device, or NULL if none.  Caller must bluetooth_device_free() it.
 */
BtDevice *bluetooth_get_connected_device(void);

/** Free a single BtDevice (not a list). */
void bluetooth_device_free(BtDevice *dev);

/** TRUE if the adapter is powered on. */
gboolean bluetooth_is_powered(void);

#endif /* BLUETOOTH_MANAGER_H */
