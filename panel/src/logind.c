#include "logind.h"
#include "venom_power.h"
#include "dbus_service.h"
#include "idle.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// ═══════════════════════════════════════════════════════════════════════════
// 💻 التكامل مع Logind
// ═══════════════════════════════════════════════════════════════════════════

gboolean logind_call(const gchar *method, GVariant *params) {
    GError *error = NULL;
    GVariant *result;

    if (!power_state.system_conn) return FALSE;

    result = g_dbus_connection_call_sync(
        power_state.system_conn, "org.freedesktop.login1", "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager", method, params,
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

    if (error) {
        fprintf(stderr, "Logind error: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }
    g_variant_unref(result);
    return TRUE;
}

gboolean logind_logout(void) {
    char *session_id = getenv("XDG_SESSION_ID");
    if (!session_id) return FALSE;
    
    char path[128];
    snprintf(path, sizeof(path), "/org/freedesktop/login1/session/%s", session_id);
    
    g_dbus_connection_call_sync(power_state.system_conn, "org.freedesktop.login1", path,
        "org.freedesktop.login1.Session", "Terminate", NULL, NULL, 
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    return TRUE;
}

gboolean logind_lock_screen(void) {
    int ret = system(LOCK_SCREEN_CMD);
    return (ret != -1);
}

void logind_on_prepare_for_sleep(GDBusConnection *connection,
                                 const gchar *sender_name,
                                 const gchar *object_path,
                                 const gchar *interface_name,
                                 const gchar *signal_name,
                                 GVariant *parameters,
                                 gpointer user_data) {
    gboolean start_sleeping;
    g_variant_get(parameters, "(b)", &start_sleeping);

    if (start_sleeping) {
        printf("💤 System is going to sleep! Launching Venom Locker...\n");
        int ret = system(LOCK_SCREEN_CMD);
        if (ret == -1) {
            fprintf(stderr, "Failed to launch lock screen\n");
        }
    } else {
        printf("☀️ System just woke up.\n");
        idle_reset_timers();
    }
}

void logind_on_properties_changed(GDBusConnection *connection,
                                  const gchar *sender_name,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *signal_name,
                                  GVariant *parameters,
                                  gpointer user_data) {
    GVariant *changed_props = NULL;
    const gchar *iface = NULL;
    
    g_variant_get(parameters, "(&s@a{sv}as)", &iface, &changed_props, NULL);
    
    if (changed_props) {
        GVariant *lid_v = g_variant_lookup_value(changed_props, "LidClosed", G_VARIANT_TYPE_BOOLEAN);
        if (lid_v) {
            gboolean closed = g_variant_get_boolean(lid_v);
            
            if (closed != power_state.lid_closed) {
                power_state.lid_closed = closed;
                printf("💻 Lid %s\n", closed ? "closed" : "opened");
                dbus_emit_signal("LidStateChanged", g_variant_new("(b)", closed));
                
                if (closed) {
                    logind_lock_screen();
                    if (power_state.on_battery) {
                        g_usleep(500000);
                        logind_call("Suspend", g_variant_new("(b)", TRUE));
                    }
                }
            }
            g_variant_unref(lid_v);
        }
        g_variant_unref(changed_props);
    }
}

void logind_setup_monitoring(void) {
    if (!power_state.system_conn) return;
    
    // مراقبة السكون
    g_dbus_connection_signal_subscribe(
        power_state.system_conn,
        "org.freedesktop.login1",
        "org.freedesktop.login1.Manager",
        "PrepareForSleep",
        "/org/freedesktop/login1",
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        logind_on_prepare_for_sleep,
        NULL, NULL
    );
    
    // مراقبة تغيير الخصائص (الغطاء)
    g_dbus_connection_signal_subscribe(
        power_state.system_conn,
        "org.freedesktop.login1",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager",
        G_DBUS_SIGNAL_FLAGS_NONE,
        logind_on_properties_changed,
        NULL, NULL
    );
    
    printf("💻 Logind monitoring active\n");
}
