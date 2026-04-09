/*
 * brightness_control.c
 *
 * Controls screen brightness via systemd-logind D-Bus interface:
 *   org.freedesktop.login1.Session.SetBrightness("backlight", device, value)
 *
 * Reads current value from /sys/class/backlight/<device>/{brightness,max_brightness}.
 * Watches for external changes via a poll timer.
 */

#include "brightness_control.h"
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib/gstdio.h>

#define BACKLIGHT_BASE "/sys/class/backlight"
#define POLL_INTERVAL_MS 2000

static BrightnessChangedCallback m_cb        = NULL;
static gpointer                  m_cb_data   = NULL;
static gchar                    *m_device    = NULL;   /* e.g. "intel_backlight" */
static long                      m_max_val   = 0;
static int                       m_last_pct  = -1;
static guint                     m_poll_id   = 0;

/* ── helpers ─────────────────────────────────────────────────────────────── */

static gchar *find_backlight_device(void)
{
    GDir   *dir   = g_dir_open(BACKLIGHT_BASE, 0, NULL);
    gchar  *found = NULL;

    if (!dir) return NULL;

    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        /* prefer firmware / platform, then intel, then acpi, then anything */
        gchar *type_path = g_build_filename(BACKLIGHT_BASE, name, "type", NULL);
        gchar *type_str  = NULL;
        g_file_get_contents(type_path, &type_str, NULL, NULL);
        g_free(type_path);

        if (type_str) {
            g_strstrip(type_str);
            gboolean is_fw = (g_strcmp0(type_str, "firmware") == 0 ||
                              g_strcmp0(type_str, "platform") == 0);
            g_free(type_str);
            if (is_fw) {
                g_free(found);
                found = g_strdup(name);
                break;
            }
        }

        if (!found) {
            found = g_strdup(name);
        }
    }
    g_dir_close(dir);
    return found;
}

static long read_sysfs_long(const gchar *device, const gchar *file)
{
    gchar *path     = g_build_filename(BACKLIGHT_BASE, device, file, NULL);
    gchar *contents = NULL;
    long   val      = -1;

    if (g_file_get_contents(path, &contents, NULL, NULL)) {
        val = atol(g_strstrip(contents));
        g_free(contents);
    }
    g_free(path);
    return val;
}

/* ── public API ──────────────────────────────────────────────────────────── */

int brightness_get(void)
{
    if (!m_device || m_max_val <= 0) return -1;

    long cur = read_sysfs_long(m_device, "brightness");
    if (cur < 0) return -1;

    int pct = (int)((cur * 100 + m_max_val / 2) / m_max_val);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

void brightness_set(int percent)
{
    if (!m_device || m_max_val <= 0) return;
    if (percent < 1)   percent = 1;   /* never go fully black */
    if (percent > 100) percent = 100;

    guint32 raw = (guint32)((long)percent * m_max_val / 100);

    /* Use systemd-logind D-Bus — works without root */
    GError         *err  = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!bus) {
        if (err) g_error_free(err);
        return;
    }

    GVariant *ret = g_dbus_connection_call_sync(
        bus,
        "org.freedesktop.login1",
        "/org/freedesktop/login1/session/auto",
        "org.freedesktop.login1.Session",
        "SetBrightness",
        g_variant_new("(ssu)", "backlight", m_device, raw),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        3000, NULL, &err);

    if (ret) {
        g_variant_unref(ret);
        m_last_pct = percent;
    } else {
        if (err) g_error_free(err);
    }
    g_object_unref(bus);
}

/* Poll sysfs so we catch changes from keyboard shortcuts etc. */
static gboolean poll_brightness(gpointer user_data)
{
    (void)user_data;
    int pct = brightness_get();
    if (pct >= 0 && pct != m_last_pct) {
        m_last_pct = pct;
        if (m_cb) m_cb(pct, m_cb_data);
    }
    return G_SOURCE_CONTINUE;
}

void brightness_init(BrightnessChangedCallback cb, gpointer user_data)
{
    m_cb      = cb;
    m_cb_data = user_data;

    m_device  = find_backlight_device();
    if (!m_device) {
        g_warning("[Brightness] No backlight device found in %s", BACKLIGHT_BASE);
        return;
    }

    m_max_val = read_sysfs_long(m_device, "max_brightness");
    if (m_max_val <= 0) {
        g_warning("[Brightness] Could not read max_brightness for %s", m_device);
        return;
    }

    /* Notify immediately with current value */
    int pct = brightness_get();
    if (pct >= 0) {
        m_last_pct = pct;
        if (m_cb) m_cb(pct, m_cb_data);
    }

    /* Poll for external changes every 2 s */
    if (m_poll_id == 0) {
        m_poll_id = g_timeout_add(POLL_INTERVAL_MS, poll_brightness, NULL);
    }
}
