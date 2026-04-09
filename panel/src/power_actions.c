#include "power_actions.h"
#include <gio/gio.h>
#include <stdio.h>
#define LOGIN1_BUS  "org.freedesktop.login1"
#define LOGIN1_PATH "/org/freedesktop/login1"
#define LOGIN1_IFACE "org.freedesktop.login1.Manager"

// إيقاف التشغيل
void venom_power_off(void) {
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (!bus) return;
    g_dbus_connection_call_sync(bus, LOGIN1_BUS, LOGIN1_PATH, LOGIN1_IFACE,
        "PowerOff",
        g_variant_new("(b)", TRUE),  
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    g_object_unref(bus);
}

// إعادة التشغيل
void venom_reboot(void) {
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (!bus) return;
    g_dbus_connection_call_sync(bus, LOGIN1_BUS, LOGIN1_PATH, LOGIN1_IFACE,
        "Reboot",
        g_variant_new("(b)", TRUE),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    g_object_unref(bus);
}

void venom_logout(void) {
    // dm-tool هي الطريقة الرسمية مع LightDM
    system("dm-tool switch-to-greeter");
}

// السكون (Sleep)
void venom_sleep(void) {
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (!bus) return;
    g_dbus_connection_call_sync(bus, LOGIN1_BUS, LOGIN1_PATH, LOGIN1_IFACE,
        "Suspend",
        g_variant_new("(b)", TRUE),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    g_object_unref(bus);
}