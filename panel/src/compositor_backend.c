#include "compositor_backend.h"
#include "aether-ipc-v1-client-protocol.h"
#include "window_backend.h"
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib-unix.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <sys/stat.h>
#include <wayland-client.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

typedef enum {
    COMPOSITOR_BACKEND_NONE = 0,
    COMPOSITOR_BACKEND_WAYFIRE,
    COMPOSITOR_BACKEND_AETHER
} CompositorBackendType;

static CompositorBackendType s_backend = COMPOSITOR_BACKEND_NONE;
static gboolean s_initialized = FALSE;

static PanelWorkspaceStateCallback s_workspace_cb = NULL;
static gpointer s_workspace_cb_data = NULL;
static PanelKeyboardStateCallback s_keyboard_cb = NULL;
static gpointer s_keyboard_cb_data = NULL;

static PanelWorkspaceState s_workspace_state = {-1, 0, 0, 0, 0};
static PanelKeyboardState s_keyboard_state = {"US", 0};
static gboolean s_have_workspace_state = FALSE;
static gboolean s_have_keyboard_state = FALSE;

/* Wayfire state */
static GSocketConnection *s_wayfire_connection = NULL;
static GInputStream *s_wayfire_input_stream = NULL;
static GOutputStream *s_wayfire_output_stream = NULL;
static GByteArray *s_wayfire_buffer = NULL;
static guint s_wayfire_io_watch_id = 0;
static guint s_wayfire_reconnect_id = 0;
static guint s_wayfire_workspace_poll_id = 0;
static gboolean s_wayfire_socket_warned = FALSE;

/* Aether Wayland protocol state */
static struct wl_display *s_wl_display = NULL;
static struct wl_registry *s_wl_registry = NULL;
static struct aether_ipc_manager_v1 *s_aether_manager = NULL;

static void emit_workspace_state(void) {
    if (s_workspace_cb && s_have_workspace_state) {
        s_workspace_cb(&s_workspace_state, s_workspace_cb_data);
    }
}

static void emit_keyboard_state(void) {
    if (s_keyboard_cb && s_have_keyboard_state) {
        s_keyboard_cb(&s_keyboard_state, s_keyboard_cb_data);
    }
}

static gchar *resolve_wayfire_socket_path(void) {
    const char *env = g_getenv("WAYFIRE_SOCKET");
    if (env && env[0]) return g_strdup(env);

    const char *runtime = g_getenv("XDG_RUNTIME_DIR");
    if (!runtime) return NULL;

    GDir *dir = g_dir_open(runtime, 0, NULL);
    const gchar *name;
    gchar *found = NULL;

    if (!dir) return NULL;

    while ((name = g_dir_read_name(dir))) {
        if (g_strrstr(name, "wayfire")) {
            gchar *path = g_build_filename(runtime, name, NULL);
            struct stat st;
            if (lstat(path, &st) == 0 && S_ISSOCK(st.st_mode)) {
                found = path;
                break;
            }
            g_free(path);
        }
    }

    g_dir_close(dir);
    return found;
}

static gboolean wayfire_ipc_call(const char *request, JsonParser **out_parser) {
    g_autofree gchar *socket_path = resolve_wayfire_socket_path();
    g_autoptr(GError) error = NULL;
    g_autoptr(GSocketClient) client = NULL;
    g_autoptr(GSocketConnectable) address = NULL;
    g_autoptr(GSocketConnection) connection = NULL;
    GOutputStream *output;
    GInputStream *input;
    guint32 length;
    guint32 response_length = 0;
    gsize written = 0;
    gsize read_bytes = 0;
    g_autofree gchar *response = NULL;
    JsonParser *parser;

    if (!socket_path || !request) return FALSE;

    client = g_socket_client_new();
    address = G_SOCKET_CONNECTABLE(g_unix_socket_address_new(socket_path));
    connection = g_socket_client_connect(client, address, NULL, &error);
    if (!connection) return FALSE;

    output = g_io_stream_get_output_stream(G_IO_STREAM(connection));
    input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    length = (guint32)strlen(request);

    if (!g_output_stream_write_all(output, &length, sizeof(length), &written, NULL, &error) ||
        written != sizeof(length)) {
        return FALSE;
    }

    if (!g_output_stream_write_all(output, request, length, &written, NULL, &error) ||
        written != length) {
        return FALSE;
    }

    if (!g_input_stream_read_all(input, &response_length, sizeof(response_length), &read_bytes, NULL, &error) ||
        read_bytes != sizeof(response_length)) {
        return FALSE;
    }

    response = g_malloc0(response_length + 1);
    if (!g_input_stream_read_all(input, response, response_length, &read_bytes, NULL, &error) ||
        read_bytes != response_length) {
        return FALSE;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, response, response_length, &error)) {
        g_object_unref(parser);
        return FALSE;
    }

    *out_parser = parser;
    return TRUE;
}

static gboolean wayfire_send_request(const char *json) {
    GError *error = NULL;
    guint32 len;
    gsize written = 0;

    if (!s_wayfire_output_stream || !json) return FALSE;

    len = (guint32)strlen(json);
    if (!g_output_stream_write_all(s_wayfire_output_stream, &len, sizeof(len), &written, NULL, &error) ||
        !g_output_stream_write_all(s_wayfire_output_stream, json, len, &written, NULL, &error)) {
        if (error) g_error_free(error);
        return FALSE;
    }
    return TRUE;
}

static void handle_wayfire_keyboard_state(JsonObject *state) {
    JsonArray *layouts;
    int idx;
    const char *raw;

    if (!state ||
        !json_object_has_member(state, "possible-layouts") ||
        !json_object_has_member(state, "layout-index")) {
        return;
    }

    layouts = json_object_get_array_member(state, "possible-layouts");
    idx = (int)json_object_get_int_member(state, "layout-index");
    if (!layouts || idx < 0 || idx >= (int)json_array_get_length(layouts)) return;

    raw = json_array_get_string_element(layouts, idx);
    if (!raw || !raw[0]) return;

    g_strlcpy(s_keyboard_state.layouts, raw, sizeof(s_keyboard_state.layouts));
    s_keyboard_state.layout_index = idx;
    s_have_keyboard_state = TRUE;
    emit_keyboard_state();
}

static gboolean wayfire_process_buffer(void) {
    if (!s_wayfire_buffer) return FALSE;

    while (s_wayfire_buffer->len >= sizeof(guint32)) {
        guint32 msg_len = 0;
        JsonParser *parser;
        JsonNode *root;
        JsonObject *obj;
        JsonObject *state = NULL;
        gchar *msg;
        GError *error = NULL;

        memcpy(&msg_len, s_wayfire_buffer->data, sizeof(msg_len));
        if (s_wayfire_buffer->len < sizeof(guint32) + msg_len) return TRUE;

        msg = g_malloc0(msg_len + 1);
        memcpy(msg, s_wayfire_buffer->data + sizeof(guint32), msg_len);
        g_byte_array_remove_range(s_wayfire_buffer, 0, sizeof(guint32) + msg_len);

        parser = json_parser_new();
        if (!json_parser_load_from_data(parser, msg, msg_len, &error)) {
            if (error) g_error_free(error);
            g_object_unref(parser);
            g_free(msg);
            continue;
        }
        g_free(msg);

        root = json_parser_get_root(parser);
        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            obj = json_node_get_object(root);
            state = obj;
            if (json_object_has_member(obj, "event")) {
                const char *ev = json_object_get_string_member(obj, "event");
                if (g_strcmp0(ev, "keyboard-modifier-state-changed") == 0 &&
                    json_object_has_member(obj, "state")) {
                    state = json_object_get_object_member(obj, "state");
                } else {
                    state = NULL;
                }
            }
            if (state && !json_object_has_member(state, "error")) {
                handle_wayfire_keyboard_state(state);
            }
        }
        g_object_unref(parser);
    }
    return TRUE;
}

static void disconnect_wayfire_keyboard_ipc(void) {
    if (s_wayfire_io_watch_id) {
        g_source_remove(s_wayfire_io_watch_id);
        s_wayfire_io_watch_id = 0;
    }
    if (s_wayfire_connection) {
        g_io_stream_close(G_IO_STREAM(s_wayfire_connection), NULL, NULL);
        g_object_unref(s_wayfire_connection);
        s_wayfire_connection = NULL;
    }
    s_wayfire_input_stream = NULL;
    s_wayfire_output_stream = NULL;
    if (s_wayfire_buffer) {
        g_byte_array_unref(s_wayfire_buffer);
        s_wayfire_buffer = NULL;
    }
}

static gboolean schedule_wayfire_reconnect(gpointer data);

static gboolean on_wayfire_socket_event(gint fd, GIOCondition cond, gpointer data) {
    GSocket *sock;
    gchar chunk[4096];

    (void)fd;
    (void)data;

    if (!s_wayfire_connection) return G_SOURCE_REMOVE;

    if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        disconnect_wayfire_keyboard_ipc();
        if (!s_wayfire_reconnect_id) {
            s_wayfire_reconnect_id = g_timeout_add(1000, schedule_wayfire_reconnect, NULL);
        }
        return G_SOURCE_REMOVE;
    }

    sock = g_socket_connection_get_socket(s_wayfire_connection);
    while (TRUE) {
        GError *error = NULL;
        gssize bytes = g_socket_receive(sock, chunk, sizeof(chunk), NULL, &error);
        if (bytes > 0) {
            g_byte_array_append(s_wayfire_buffer, (const guint8 *)chunk, (guint)bytes);
            continue;
        }
        if (bytes == 0) {
            disconnect_wayfire_keyboard_ipc();
            if (!s_wayfire_reconnect_id) {
                s_wayfire_reconnect_id = g_timeout_add(1000, schedule_wayfire_reconnect, NULL);
            }
            return G_SOURCE_REMOVE;
        }
        if (error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
            g_error_free(error);
            break;
        }
        if (error) g_error_free(error);
        disconnect_wayfire_keyboard_ipc();
        if (!s_wayfire_reconnect_id) {
            s_wayfire_reconnect_id = g_timeout_add(1000, schedule_wayfire_reconnect, NULL);
        }
        return G_SOURCE_REMOVE;
    }

    wayfire_process_buffer();
    return G_SOURCE_CONTINUE;
}

static gboolean connect_wayfire_keyboard_ipc(void) {
    g_autofree gchar *socket_path = resolve_wayfire_socket_path();
    GSocketClient *client;
    GSocketAddress *address;
    GSocketConnection *conn;
    GError *error = NULL;
    GSocket *sock;

    if (s_wayfire_connection) return TRUE;
    if (!socket_path || !socket_path[0]) {
        if (!s_wayfire_socket_warned) {
            g_warning("[panel] WAYFIRE_SOCKET not found; Wayfire IPC disabled");
            s_wayfire_socket_warned = TRUE;
        }
        return FALSE;
    }

    client = g_socket_client_new();
    address = g_unix_socket_address_new(socket_path);
    conn = g_socket_client_connect(client, G_SOCKET_CONNECTABLE(address), NULL, &error);
    g_object_unref(address);
    g_object_unref(client);

    if (!conn) {
        if (error) g_error_free(error);
        return FALSE;
    }

    sock = g_socket_connection_get_socket(conn);
    g_socket_set_blocking(sock, FALSE);

    s_wayfire_connection = conn;
    s_wayfire_input_stream = g_io_stream_get_input_stream(G_IO_STREAM(conn));
    s_wayfire_output_stream = g_io_stream_get_output_stream(G_IO_STREAM(conn));
    s_wayfire_buffer = g_byte_array_new();
    s_wayfire_socket_warned = FALSE;
    s_wayfire_io_watch_id = g_unix_fd_add(g_socket_get_fd(sock),
                                          G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                          on_wayfire_socket_event, NULL);

    wayfire_send_request("{\"method\":\"window-rules/events/watch\",\"events\":[\"keyboard-modifier-state-changed\"]}");
    wayfire_send_request("{\"method\":\"wayfire/get-keyboard-state\"}");
    return TRUE;
}

static gboolean schedule_wayfire_reconnect(gpointer data) {
    (void)data;
    s_wayfire_reconnect_id = 0;
    return connect_wayfire_keyboard_ipc() ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

static gboolean refresh_wayfire_workspaces(gpointer user_data) {
    JsonParser *parser = NULL;
    JsonNode *root;
    JsonArray *array;
    JsonObject *wset = NULL;
    guint i;

    (void)user_data;

    if (!wayfire_ipc_call("{\"method\":\"window-rules/list-wsets\"}", &parser)) {
        return G_SOURCE_CONTINUE;
    }

    root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_ARRAY(root)) {
        g_object_unref(parser);
        return G_SOURCE_CONTINUE;
    }

    array = json_node_get_array(root);
    for (i = 0; i < json_array_get_length(array); i++) {
        JsonObject *item = json_array_get_object_element(array, i);
        if (!item || !json_object_has_member(item, "workspace")) continue;
        wset = item;
        break;
    }

    if (wset) {
        JsonObject *workspace = json_object_get_object_member(wset, "workspace");
        if (workspace) {
            s_workspace_state.output_id = json_object_get_int_member(wset, "output-id");
            s_workspace_state.x = json_object_get_int_member(workspace, "x");
            s_workspace_state.y = json_object_get_int_member(workspace, "y");
            s_workspace_state.grid_width = json_object_get_int_member(workspace, "grid_width");
            s_workspace_state.grid_height = json_object_get_int_member(workspace, "grid_height");
            s_have_workspace_state = TRUE;
            emit_workspace_state();
        }
    }

    g_object_unref(parser);
    return G_SOURCE_CONTINUE;
}

static void handle_aether_workspace_state(void *data,
                                          struct aether_ipc_manager_v1 *manager,
                                          int32_t output_id,
                                          int32_t x,
                                          int32_t y,
                                          int32_t grid_width,
                                          int32_t grid_height) {
    (void)data;
    (void)manager;

    s_workspace_state.output_id = output_id;
    s_workspace_state.x = x;
    s_workspace_state.y = y;
    s_workspace_state.grid_width = grid_width;
    s_workspace_state.grid_height = grid_height;
    s_have_workspace_state = TRUE;
    emit_workspace_state();
}

static void handle_aether_keyboard_state(void *data,
                                         struct aether_ipc_manager_v1 *manager,
                                         const char *layouts,
                                         int32_t layout_index) {
    (void)data;
    (void)manager;

    g_strlcpy(s_keyboard_state.layouts, layouts ? layouts : "", sizeof(s_keyboard_state.layouts));
    s_keyboard_state.layout_index = layout_index;
    s_have_keyboard_state = TRUE;
    emit_keyboard_state();
}

static const struct aether_ipc_manager_v1_listener s_aether_listener = {
    .workspace_state = handle_aether_workspace_state,
    .keyboard_state = handle_aether_keyboard_state,
};

static void on_registry_global(void *data,
                               struct wl_registry *registry,
                               uint32_t name,
                               const char *interface,
                               uint32_t version) {
    (void)data;

    if (g_strcmp0(interface, aether_ipc_manager_v1_interface.name) == 0) {
        s_aether_manager = wl_registry_bind(registry,
                                            name,
                                            &aether_ipc_manager_v1_interface,
                                            MIN(version, 1u));
        aether_ipc_manager_v1_add_listener(s_aether_manager, &s_aether_listener, NULL);
    }
}

static void on_registry_global_remove(void *data,
                                      struct wl_registry *registry,
                                      uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener s_registry_listener = {
    .global = on_registry_global,
    .global_remove = on_registry_global_remove,
};

static gboolean init_aether_backend(void) {
#ifdef GDK_WINDOWING_WAYLAND
    GdkDisplay *gdk_display;

    if (!panel_window_backend_is_wayland()) return FALSE;

    gdk_display = gdk_display_get_default();
    if (!gdk_display || !GDK_IS_WAYLAND_DISPLAY(gdk_display)) return FALSE;

    s_wl_display = gdk_wayland_display_get_wl_display(gdk_display);
    if (!s_wl_display) return FALSE;

    s_wl_registry = wl_display_get_registry(s_wl_display);
    if (!s_wl_registry) return FALSE;

    wl_registry_add_listener(s_wl_registry, &s_registry_listener, NULL);
    wl_display_roundtrip(s_wl_display);
    if (!s_aether_manager) return FALSE;
    wl_display_roundtrip(s_wl_display);
    return TRUE;
#else
    return FALSE;
#endif
}

void panel_compositor_backend_init(void) {
    if (s_initialized) return;
    s_initialized = TRUE;

    if (init_aether_backend()) {
        s_backend = COMPOSITOR_BACKEND_AETHER;
        return;
    }

    if (resolve_wayfire_socket_path() != NULL) {
        s_backend = COMPOSITOR_BACKEND_WAYFIRE;
        connect_wayfire_keyboard_ipc();
        s_wayfire_workspace_poll_id = g_timeout_add(700, refresh_wayfire_workspaces, NULL);
        refresh_wayfire_workspaces(NULL);
        return;
    }
}

void panel_compositor_backend_set_workspace_callback(PanelWorkspaceStateCallback cb, gpointer user_data) {
    s_workspace_cb = cb;
    s_workspace_cb_data = user_data;
    if (s_have_workspace_state) emit_workspace_state();
}

void panel_compositor_backend_set_keyboard_callback(PanelKeyboardStateCallback cb, gpointer user_data) {
    s_keyboard_cb = cb;
    s_keyboard_cb_data = user_data;
    if (s_have_keyboard_state) emit_keyboard_state();
}

gboolean panel_compositor_backend_set_workspace(int output_id, int x, int y) {
    if (s_backend == COMPOSITOR_BACKEND_AETHER && s_aether_manager) {
        aether_ipc_manager_v1_set_workspace(s_aether_manager, output_id, x, y);
#ifdef GDK_WINDOWING_WAYLAND
        if (s_wl_display) wl_display_flush(s_wl_display);
#endif
        return TRUE;
    }

    if (s_backend == COMPOSITOR_BACKEND_WAYFIRE) {
        char request[256];
        JsonParser *parser = NULL;

        g_snprintf(request, sizeof(request),
                   "{\"method\":\"vswitch/set-workspace\",\"data\":{\"x\":%d,\"y\":%d,\"output-id\":%d}}",
                   x, y, output_id);

        if (!wayfire_ipc_call(request, &parser)) return FALSE;
        if (parser) g_object_unref(parser);
        return TRUE;
    }

    return FALSE;
}
