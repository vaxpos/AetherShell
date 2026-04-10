/*
 * ═══════════════════════════════════════════════════════════════════════════
 * 🔍 Venom Basilisk - Main Entry Point & D-Bus Server
 * ═══════════════════════════════════════════════════════════════════════════
 * Venom Basilisk application launcher for Venom Desktop
 * Based on venom_clipboard structure
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include "basilisk.h"
#include "window.h"
#include "search.h"

// Global State
BasiliskState *state = NULL;

// ═══════════════════════════════════════════════════════════════════════════
// D-Bus Interface (like venom_clipboard)
// ═══════════════════════════════════════════════════════════════════════════

static const gchar introspection_xml[] =
    "<node><interface name='org.venom.Basilisk'>"
    "<method name='Show'/><method name='Hide'/><method name='Toggle'/>"
    "<method name='Search'><arg type='s' name='query' direction='in'/></method>"
    "</interface></node>";

static void handle_method_call(
    GDBusConnection *c, const gchar *s, const gchar *o, const gchar *i,
    const gchar *m, GVariant *p, GDBusMethodInvocation *inv, gpointer u)
{
    (void)c; (void)s; (void)o; (void)i; (void)u;
    
    g_print("📨 D-Bus method called: %s (visible=%d)\n", m, state->visible);
    
    // Show or Toggle when hidden
    if (g_strcmp0(m, "Show") == 0 || (g_strcmp0(m, "Toggle") == 0 && !state->visible)) {
        extern void window_show(void);
        window_show();
    }
    // Hide or Toggle when visible
    else if (g_strcmp0(m, "Hide") == 0 || (g_strcmp0(m, "Toggle") == 0 && state->visible)) {
        extern void window_hide(void);
        window_hide();
    }
    // Search
    else if (g_strcmp0(m, "Search") == 0) {
        const gchar *query;
        g_variant_get(p, "(&s)", &query);
        
        gtk_entry_set_text(GTK_ENTRY(state->search_entry), query);
        extern void window_show(void);
        window_show();
    }
    
    g_dbus_method_invocation_return_value(inv, NULL);
}

static const GDBusInterfaceVTable vtable = {handle_method_call, NULL, NULL, {0}};

static void on_bus_acquired(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    
    GError *error = NULL;
    GDBusNodeInfo *node_info = g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (error) {
        g_warning("D-Bus XML error: %s", error->message);
        g_error_free(error);
        return;
    }
    
    g_dbus_connection_register_object(conn, DBUS_PATH, node_info->interfaces[0],
        &vtable, NULL, NULL, &error);
    
    if (error) {
        g_warning("D-Bus register error: %s", error->message);
        g_error_free(error);
    }
    
    state->dbus_conn = conn;
    g_dbus_node_info_unref(node_info);
    g_print("📡 D-Bus ready: %s\n", DBUS_NAME);
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char *argv[]) {
    g_print("🚀 ════════════════════════════════════════════════════════════\n");
    g_print("🚀 Venom Basilisk - Spotlight Search\n");
    g_print("🚀 ════════════════════════════════════════════════════════════\n");
    
    gtk_init(&argc, &argv);
    
    // Initialize state
    state = g_new0(BasiliskState, 1);
    state->visible = FALSE;
    
    // Initialize modules (build window FIRST)
    search_init();
    window_init();
    
    g_print("🔍 Window built\n");
    
    // D-Bus
    state->dbus_owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION, DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired, NULL, NULL, NULL, NULL);
    
    // Check --show argument
    gboolean show_on_start = FALSE;
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--show") == 0 || g_strcmp0(argv[i], "-s") == 0) {
            show_on_start = TRUE;
            break;
        }
    }
    
    if (show_on_start) {
        extern void window_show(void);
        window_show();
    }
    
    g_print("✅ Ready. Use D-Bus Toggle or --show\n");
    
    gtk_main();
    
    // Cleanup
    if (state->dbus_owner_id > 0) g_bus_unown_name(state->dbus_owner_id);
    search_free_apps();
    g_free(state);
    
    return 0;
}
