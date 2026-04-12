#include "osd_protocols.h"
#include "osd_protocols_x11.h"
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib-unix.h>
#include <wayland-client.h>
#include "aether-ipc-v1-client-protocol.h"
#include <sys/stat.h>

// Wayfire JSON IPC globals
static gboolean wayfire_socket_warned = FALSE;
static GSocketConnection *wayfire_connection = NULL;
static GInputStream *wayfire_input = NULL;
static GOutputStream *wayfire_output = NULL;
static GByteArray *wayfire_buffer = NULL;
static guint wayfire_io_watch_id = 0;
static guint wayfire_reconnect_id = 0;

// Aether Wayland IPC globals
static gboolean aether_global_warned = FALSE;
static struct wl_display *aether_display = NULL;
static struct wl_registry *aether_registry = NULL;
static struct aether_ipc_manager_v1 *aether_manager = NULL;
static guint aether_io_watch_id = 0;
static guint aether_reconnect_id = 0;

static OsdProtocolCallbacks callbacks = {0};

static gboolean connect_wayfire_ipc(void);
static gboolean connect_aether_ipc(void);

static gboolean is_wayfire_socket_candidate(const char *path, const char *name) {
    if (!path || !name) return FALSE;
    if (!g_strrstr(name, "wayfire")) return FALSE;

    struct stat st;
    if (lstat(path, &st) != 0) return FALSE;

    return S_ISSOCK(st.st_mode);
}

static gchar *find_wayfire_socket_in_dir(const char *dir_path, int depth) {
    if (!dir_path || !dir_path[0] || depth < 0) return NULL;

    GError *error = NULL;
    GDir *dir = g_dir_open(dir_path, 0, &error);
    if (!dir) {
        if (error) g_error_free(error);
        return NULL;
    }

    const gchar *name = NULL;
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *path = g_build_filename(dir_path, name, NULL);

        if (is_wayfire_socket_candidate(path, name)) {
            g_dir_close(dir);
            return path;
        }

        if (depth > 0 && g_file_test(path, G_FILE_TEST_IS_DIR)) {
            gchar *nested = find_wayfire_socket_in_dir(path, depth - 1);
            g_free(path);
            if (nested) {
                g_dir_close(dir);
                return nested;
            }
            continue;
        }

        g_free(path);
    }

    g_dir_close(dir);
    return NULL;
}

static gchar *resolve_wayfire_socket_path(void) {
    const char *socket_path = g_getenv("WAYFIRE_SOCKET");
    if (socket_path && socket_path[0]) {
        return g_strdup(socket_path);
    }

    const char *runtime_dir = g_getenv("XDG_RUNTIME_DIR");
    gchar *detected = find_wayfire_socket_in_dir(runtime_dir, 1);
    if (detected) return detected;

    return find_wayfire_socket_in_dir("/tmp", 1);
}

static gboolean handle_wayfire_message(JsonObject *obj, gboolean show_popup) {
    if (!obj) return FALSE;
    if (json_object_has_member(obj, "error")) return FALSE;
    if (!callbacks.update_layouts_from_json_array || !callbacks.apply_keyboard_layout_state) return FALSE;

    JsonObject *state = obj;
    if (json_object_has_member(obj, "event")) {
        const char *event_name = json_object_get_string_member(obj, "event");
        if (!g_strcmp0(event_name, "keyboard-modifier-state-changed")) {
            if (!json_object_has_member(obj, "state")) return FALSE;
            state = json_object_get_object_member(obj, "state");
        }
    }

    if (!json_object_has_member(state, "possible-layouts") ||
        !json_object_has_member(state, "layout-index")) {
        return FALSE;
    }

    JsonArray *layouts = json_object_get_array_member(state, "possible-layouts");
    callbacks.update_layouts_from_json_array(layouts);
    return callbacks.apply_keyboard_layout_state(json_object_get_int_member(state, "layout-index"), show_popup);
}

static gboolean process_wayfire_buffer(gboolean show_popup) {
    if (!wayfire_buffer) return FALSE;

    while (wayfire_buffer->len >= sizeof(guint32)) {
        guint32 message_len = 0;
        memcpy(&message_len, wayfire_buffer->data, sizeof(message_len));

        if (wayfire_buffer->len < sizeof(guint32) + message_len) {
            return TRUE;
        }

        gchar *message = g_malloc0(message_len + 1);
        memcpy(message, wayfire_buffer->data + sizeof(guint32), message_len);
        g_byte_array_remove_range(wayfire_buffer, 0, sizeof(guint32) + message_len);

        GError *error = NULL;
        JsonParser *parser = json_parser_new();
        gboolean parsed = json_parser_load_from_data(parser, message, message_len, &error);
        g_free(message);

        if (!parsed) {
            g_warning("[OSD] Failed to parse Wayfire IPC JSON: %s",
                error ? error->message : "unknown error");
            if (error) g_error_free(error);
            g_object_unref(parser);
            continue;
        }

        JsonNode *root = json_parser_get_root(parser);
        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            handle_wayfire_message(json_node_get_object(root), show_popup);
        }

        g_object_unref(parser);
    }

    return TRUE;
}

static gboolean send_wayfire_request(const char *json) {
    if (!wayfire_output || !json) return FALSE;

    GError *error = NULL;
    guint32 request_len = (guint32)strlen(json);
    gsize bytes_written = 0;
    gboolean ok =
        g_output_stream_write_all(wayfire_output, &request_len, sizeof(request_len),
            &bytes_written, NULL, &error) &&
        g_output_stream_write_all(wayfire_output, json, request_len,
            &bytes_written, NULL, &error);

    if (!ok) {
        g_warning("[OSD] Failed to send Wayfire IPC request: %s",
            error ? error->message : "unknown error");
        if (error) g_error_free(error);
        return FALSE;
    }

    return TRUE;
}

static void disconnect_wayfire_ipc(void) {
    if (wayfire_io_watch_id) {
        g_source_remove(wayfire_io_watch_id);
        wayfire_io_watch_id = 0;
    }

    if (wayfire_connection) {
        g_io_stream_close(G_IO_STREAM(wayfire_connection), NULL, NULL);
        g_object_unref(wayfire_connection);
        wayfire_connection = NULL;
    }

    wayfire_input = NULL;
    wayfire_output = NULL;

    if (wayfire_buffer) {
        g_byte_array_unref(wayfire_buffer);
        wayfire_buffer = NULL;
    }
}

static gboolean on_wayfire_socket_event(gint fd, GIOCondition condition, gpointer data);

static gboolean schedule_wayfire_reconnect(gpointer data) {
    (void)data;
    wayfire_reconnect_id = 0;
    return connect_wayfire_ipc() ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

static gboolean connect_wayfire_ipc(void) {
    if (wayfire_connection) return TRUE;

    g_autofree gchar *socket_path = resolve_wayfire_socket_path();
    if (!socket_path || !socket_path[0]) {
        if (!wayfire_socket_warned) {
            g_warning("[OSD] Could not find WAYFIRE_SOCKET; keyboard OSD disabled");
            wayfire_socket_warned = TRUE;
        }
        return FALSE;
    }

    GError *error = NULL;
    GSocketClient *client = g_socket_client_new();
    GSocketAddress *address = g_unix_socket_address_new(socket_path);
    GSocketConnection *connection = g_socket_client_connect(client,
        G_SOCKET_CONNECTABLE(address), NULL, &error);
    g_object_unref(address);
    g_object_unref(client);

    if (!connection) {
        if (error) {
            g_warning("[OSD] Failed to connect to Wayfire IPC: %s", error->message);
            g_error_free(error);
        }
        return FALSE;
    }

    GSocket *socket = g_socket_connection_get_socket(connection);
    g_socket_set_blocking(socket, FALSE);

    wayfire_connection = connection;
    wayfire_input = g_io_stream_get_input_stream(G_IO_STREAM(wayfire_connection));
    wayfire_output = g_io_stream_get_output_stream(G_IO_STREAM(wayfire_connection));
    wayfire_buffer = g_byte_array_new();
    wayfire_socket_warned = FALSE;

    wayfire_io_watch_id = g_unix_fd_add(g_socket_get_fd(socket),
        G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL, on_wayfire_socket_event, NULL);

    send_wayfire_request(
        "{\"method\":\"window-rules/events/watch\",\"events\":[\"keyboard-modifier-state-changed\"]}");
    send_wayfire_request("{\"method\":\"wayfire/get-keyboard-state\"}");
    return TRUE;
}

static gboolean on_wayfire_socket_event(gint fd, GIOCondition condition, gpointer data) {
    (void)fd;
    (void)data;

    if (!wayfire_connection) return G_SOURCE_REMOVE;

    if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        disconnect_wayfire_ipc();
        if (!wayfire_reconnect_id) {
            wayfire_reconnect_id = g_timeout_add(1000, schedule_wayfire_reconnect, NULL);
        }
        return G_SOURCE_REMOVE;
    }

    GSocket *socket = g_socket_connection_get_socket(wayfire_connection);
    gchar chunk[4096];

    while (TRUE) {
        GError *error = NULL;
        gssize bytes = g_socket_receive(socket, chunk, sizeof(chunk), NULL, &error);
        if (bytes > 0) {
            g_byte_array_append(wayfire_buffer, (const guint8*)chunk, (guint)bytes);
            continue;
        }

        if (bytes == 0) {
            disconnect_wayfire_ipc();
            if (!wayfire_reconnect_id) {
                wayfire_reconnect_id = g_timeout_add(1000, schedule_wayfire_reconnect, NULL);
            }
            return G_SOURCE_REMOVE;
        }

        if (error &&
            g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
            g_error_free(error);
            break;
        }

        g_warning("[OSD] Wayfire IPC read failed: %s",
            error ? error->message : "unknown error");
        if (error) g_error_free(error);
        disconnect_wayfire_ipc();
        if (!wayfire_reconnect_id) {
            wayfire_reconnect_id = g_timeout_add(1000, schedule_wayfire_reconnect, NULL);
        }
        return G_SOURCE_REMOVE;
    }

    process_wayfire_buffer(TRUE);
    return G_SOURCE_CONTINUE;
}

static void aether_handle_keyboard_state(void *data,
    struct aether_ipc_manager_v1 *manager,
    const char *layouts,
    int32_t layout_index) {
    (void)data;
    (void)manager;

    if (!callbacks.update_layouts_from_delimited_string ||
        !callbacks.apply_keyboard_layout_state) {
        return;
    }

    if (!callbacks.update_layouts_from_delimited_string(layouts, '\n')) return;
    callbacks.apply_keyboard_layout_state(layout_index, TRUE);
}

static void aether_handle_workspace_state(void *data,
    struct aether_ipc_manager_v1 *manager,
    int32_t output_id,
    int32_t x,
    int32_t y,
    int32_t grid_width,
    int32_t grid_height) {
    (void)data;
    (void)manager;
    (void)output_id;
    (void)x;
    (void)y;
    (void)grid_width;
    (void)grid_height;
}

static const struct aether_ipc_manager_v1_listener aether_manager_listener = {
    .workspace_state = aether_handle_workspace_state,
    .keyboard_state = aether_handle_keyboard_state,
};

static void aether_registry_global(void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version) {
    (void)data;

    if (g_strcmp0(interface, aether_ipc_manager_v1_interface.name) != 0 || aether_manager) {
        return;
    }

    uint32_t bind_version = version < 1 ? version : 1;
    aether_manager = wl_registry_bind(registry, name, &aether_ipc_manager_v1_interface, bind_version);
    if (aether_manager) {
        aether_ipc_manager_v1_add_listener(aether_manager, &aether_manager_listener, NULL);
        aether_global_warned = FALSE;
    }
}

static void aether_registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener aether_registry_listener = {
    .global = aether_registry_global,
    .global_remove = aether_registry_global_remove,
};

static void disconnect_aether_ipc(void) {
    if (aether_io_watch_id) {
        g_source_remove(aether_io_watch_id);
        aether_io_watch_id = 0;
    }

    if (aether_manager) {
        aether_ipc_manager_v1_destroy(aether_manager);
        aether_manager = NULL;
    }

    if (aether_registry) {
        wl_registry_destroy(aether_registry);
        aether_registry = NULL;
    }

    if (aether_display) {
        wl_display_disconnect(aether_display);
        aether_display = NULL;
    }
}

static gboolean on_aether_display_event(gint fd, GIOCondition condition, gpointer data);

static gboolean schedule_aether_reconnect(gpointer data) {
    (void)data;
    aether_reconnect_id = 0;
    return connect_aether_ipc() ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

static gboolean connect_aether_ipc(void) {
    if (aether_display) return TRUE;
    if (callbacks.is_wayland_session && !callbacks.is_wayland_session()) return FALSE;

    aether_display = wl_display_connect(NULL);
    if (!aether_display) {
        if (!aether_global_warned) {
            g_warning("[OSD] Failed to connect to Wayland display for Aether IPC");
            aether_global_warned = TRUE;
        }
        return FALSE;
    }

    aether_registry = wl_display_get_registry(aether_display);
    if (!aether_registry) {
        g_warning("[OSD] Failed to get Wayland registry for Aether IPC");
        disconnect_aether_ipc();
        return FALSE;
    }

    wl_registry_add_listener(aether_registry, &aether_registry_listener, NULL);

    if (wl_display_roundtrip(aether_display) < 0) {
        g_warning("[OSD] Failed initial Wayland roundtrip for Aether IPC");
        disconnect_aether_ipc();
        return FALSE;
    }

    if (!aether_manager) {
        if (!aether_global_warned) {
            g_warning("[OSD] aether_ipc_manager_v1 not advertised; Aether keyboard OSD disabled");
            aether_global_warned = TRUE;
        }
        disconnect_aether_ipc();
        return FALSE;
    }

    if (wl_display_roundtrip(aether_display) < 0) {
        g_warning("[OSD] Failed to receive initial Aether IPC state");
        disconnect_aether_ipc();
        return FALSE;
    }

    int fd = wl_display_get_fd(aether_display);
    if (fd < 0) {
        g_warning("[OSD] Failed to get Wayland fd for Aether IPC");
        disconnect_aether_ipc();
        return FALSE;
    }

    aether_io_watch_id = g_unix_fd_add(fd,
        G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL, on_aether_display_event, NULL);
    aether_global_warned = FALSE;
    return TRUE;
}

static gboolean on_aether_display_event(gint fd, GIOCondition condition, gpointer data) {
    (void)fd;
    (void)data;

    if (!aether_display) return G_SOURCE_REMOVE;

    if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        disconnect_aether_ipc();
        if (!aether_reconnect_id) {
            aether_reconnect_id = g_timeout_add(1000, schedule_aether_reconnect, NULL);
        }
        return G_SOURCE_REMOVE;
    }

    if (wl_display_dispatch(aether_display) < 0) {
        g_warning("[OSD] Aether IPC dispatch failed");
        disconnect_aether_ipc();
        if (!aether_reconnect_id) {
            aether_reconnect_id = g_timeout_add(1000, schedule_aether_reconnect, NULL);
        }
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

void osd_protocols_init(const OsdProtocolCallbacks *cb) {
    if (cb) {
        callbacks = *cb;
    }

    if (!connect_wayfire_ipc() && !wayfire_reconnect_id) {
        wayfire_reconnect_id = g_timeout_add(1000, schedule_wayfire_reconnect, NULL);
    }

    if (!connect_aether_ipc() && !aether_reconnect_id) {
        aether_reconnect_id = g_timeout_add(1000, schedule_aether_reconnect, NULL);
    }

    osd_protocols_x11_init(&callbacks);
}

void osd_protocols_shutdown(void) {
    osd_protocols_x11_shutdown();
    if (wayfire_reconnect_id) {
        g_source_remove(wayfire_reconnect_id);
        wayfire_reconnect_id = 0;
    }
    if (aether_reconnect_id) {
        g_source_remove(aether_reconnect_id);
        aether_reconnect_id = 0;
    }
    disconnect_wayfire_ipc();
    disconnect_aether_ipc();
}
