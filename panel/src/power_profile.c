#include "power_profile.h"
#include <gio/gio.h>

static GDBusProxy *m_proxy = NULL;
static PowerProfileChangedCallback m_callback = NULL;
static gpointer m_callback_data = NULL;

static void on_properties_changed(GDBusProxy *proxy, GVariant *changed_properties, GStrv invalidated_properties, gpointer user_data) {
    if (!changed_properties) return;
    
    GVariant *val = g_variant_lookup_value(changed_properties, "ActiveProfile", G_VARIANT_TYPE_STRING);
    if (val) {
        if (m_callback) {
            m_callback(g_variant_get_string(val, NULL), m_callback_data);
        }
        g_variant_unref(val);
    }
}

static void on_proxy_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    m_proxy = g_dbus_proxy_new_for_bus_finish(res, &error);
    if (!m_proxy) {
        g_printerr("Error creating power profile proxy: %s\n", error->message);
        g_error_free(error);
        return;
    }

    g_signal_connect(m_proxy, "g-properties-changed", G_CALLBACK(on_properties_changed), NULL);

    GVariant *v = g_dbus_proxy_get_cached_property(m_proxy, "ActiveProfile");
    if (v) {
        if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) {
            if (m_callback) {
                m_callback(g_variant_get_string(v, NULL), m_callback_data);
            }
        }
        g_variant_unref(v);
    }
}

void power_profile_init(PowerProfileChangedCallback cb, gpointer user_data) {
    m_callback = cb;
    m_callback_data = user_data;

    g_dbus_proxy_new_for_bus(G_BUS_TYPE_SYSTEM,
                             G_DBUS_PROXY_FLAGS_NONE,
                             NULL,
                             "net.hadess.PowerProfiles",
                             "/net/hadess/PowerProfiles",
                             "net.hadess.PowerProfiles",
                             NULL,
                             on_proxy_ready,
                             NULL);
}

void power_profile_set(const char *profile) {
    if (!m_proxy) return;

    g_dbus_proxy_call(m_proxy,
                      "org.freedesktop.DBus.Properties.Set",
                      g_variant_new("(ssv)", "net.hadess.PowerProfiles", "ActiveProfile", g_variant_new_string(profile)),
                      G_DBUS_CALL_FLAGS_NONE,
                      -1,
                      NULL,
                      NULL,
                      NULL);
}
