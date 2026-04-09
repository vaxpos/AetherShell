/*
 * keyboard_layout.c — Keyboard layout indicator for the Aether panel
 *
 * Uses the same persistent Wayfire IPC connection pattern as vaxpwy-osd-notify.
 * Subscribes to "keyboard-modifier-state-changed" events via
 *   window-rules/events/watch
 * and polls the initial state via
 *   wayfire/get-keyboard-state
 * Reconnects automatically on socket disconnect.
 */

#include <gtk/gtk.h>
#include <glib-unix.h>
#include <gio/gunixsocketaddress.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include "keyboard_layout.h"

/* ── widget list ────────────────────────────────────────────────────────── */
typedef struct {
    GtkWidget *box;
    GtkWidget *label;
} KeyboardLayoutWidget;

static GList  *s_widgets        = NULL;
static char    s_current_layout[16] = "US";

/* ── Wayfire IPC state ──────────────────────────────────────────────────── */
static GSocketConnection *s_connection       = NULL;
static GInputStream      *s_input_stream     = NULL;
static GOutputStream     *s_output_stream    = NULL;
static GByteArray        *s_buffer           = NULL;
static guint              s_io_watch_id      = 0;
static guint              s_reconnect_id     = 0;
static gboolean           s_socket_warned    = FALSE;

/* forward declarations */
static gboolean connect_wayfire_ipc(void);
static void     disconnect_wayfire_ipc(void);
static gboolean schedule_reconnect(gpointer data);
static gboolean on_socket_event(gint fd, GIOCondition cond, gpointer data);
static gboolean process_buffer(void);
static gboolean send_request(const char *json);
static void     update_all_widgets(void);

/* ── layout name helpers ────────────────────────────────────────────────── */

static const char *display_layout_name(const char *name) {
    if (!name || !name[0]) return NULL;
    if (g_ascii_strcasecmp(name, "AR")  == 0 ||
        g_ascii_strcasecmp(name, "ARA") == 0 ||
        g_ascii_strcasecmp(name, "IQ")  == 0 ||
        g_ascii_strcasecmp(name, "Arabic") == 0)
        return "AR";
    if (g_ascii_strcasecmp(name, "US")  == 0 ||
        g_ascii_strcasecmp(name, "USA") == 0 ||
        g_ascii_strcasecmp(name, "EN")  == 0 ||
        g_ascii_strcasecmp(name, "English") == 0)
        return "US";
    return name;
}

static void normalize_layout(char *layout) {
    if (!layout || !layout[0]) return;
    for (char *p = layout; *p; p++) *p = g_ascii_toupper(*p);
    if (strcmp(layout, "ARA") == 0 || strcmp(layout, "IQ") == 0)
        strcpy(layout, "AR");
    else if (strcmp(layout, "USA") == 0)
        strcpy(layout, "US");
}

/* ── message handling ───────────────────────────────────────────────────── */

static void handle_keyboard_state(JsonObject *state) {
    if (!state) return;
    if (!json_object_has_member(state, "possible-layouts") ||
        !json_object_has_member(state, "layout-index"))
        return;

    JsonArray *layouts = json_object_get_array_member(state, "possible-layouts");
    int idx = (int)json_object_get_int_member(state, "layout-index");
    if (!layouts || idx < 0 || idx >= (int)json_array_get_length(layouts))
        return;

    const char *raw = json_array_get_string_element(layouts, idx);
    if (!raw || !raw[0]) return;

    const char *display = display_layout_name(raw);
    g_strlcpy(s_current_layout, display ? display : raw, sizeof(s_current_layout));
    normalize_layout(s_current_layout);
    update_all_widgets();
}

static gboolean process_buffer(void) {
    if (!s_buffer) return FALSE;

    while (s_buffer->len >= sizeof(guint32)) {
        guint32 msg_len = 0;
        memcpy(&msg_len, s_buffer->data, sizeof(msg_len));

        if (s_buffer->len < sizeof(guint32) + msg_len)
            return TRUE;

        gchar *msg = g_malloc0(msg_len + 1);
        memcpy(msg, s_buffer->data + sizeof(guint32), msg_len);
        g_byte_array_remove_range(s_buffer, 0, sizeof(guint32) + msg_len);

        GError    *error  = NULL;
        JsonParser *parser = json_parser_new();
        if (!json_parser_load_from_data(parser, msg, msg_len, &error)) {
            g_warning("[keyboard-layout] JSON parse error: %s",
                      error ? error->message : "unknown");
            if (error) g_error_free(error);
            g_object_unref(parser);
            g_free(msg);
            continue;
        }
        g_free(msg);

        JsonNode *root = json_parser_get_root(parser);
        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            JsonObject *obj = json_node_get_object(root);
            JsonObject *state = obj;

            /* check if it's an event wrapper */
            if (json_object_has_member(obj, "event")) {
                const char *ev = json_object_get_string_member(obj, "event");
                if (g_strcmp0(ev, "keyboard-modifier-state-changed") == 0 &&
                    json_object_has_member(obj, "state"))
                    state = json_object_get_object_member(obj, "state");
                else
                    state = NULL;
            }

            if (state && !json_object_has_member(state, "error"))
                handle_keyboard_state(state);
        }
        g_object_unref(parser);
    }
    return TRUE;
}

/* ── socket helpers ─────────────────────────────────────────────────────── */

static gboolean send_request(const char *json) {
    if (!s_output_stream || !json) return FALSE;
    GError  *error   = NULL;
    guint32  len     = (guint32)strlen(json);
    gsize    written = 0;
    gboolean ok =
        g_output_stream_write_all(s_output_stream, &len, sizeof(len), &written, NULL, &error) &&
        g_output_stream_write_all(s_output_stream, json, len,        &written, NULL, &error);
    if (!ok) {
        g_warning("[keyboard-layout] Send failed: %s",
                  error ? error->message : "unknown");
        if (error) g_error_free(error);
    }
    return ok;
}

static void disconnect_wayfire_ipc(void) {
    if (s_io_watch_id) {
        g_source_remove(s_io_watch_id);
        s_io_watch_id = 0;
    }
    if (s_connection) {
        g_io_stream_close(G_IO_STREAM(s_connection), NULL, NULL);
        g_object_unref(s_connection);
        s_connection = NULL;
    }
    s_input_stream  = NULL;
    s_output_stream = NULL;
    if (s_buffer) {
        g_byte_array_unref(s_buffer);
        s_buffer = NULL;
    }
}

static gboolean schedule_reconnect(gpointer data) {
    (void)data;
    s_reconnect_id = 0;
    return connect_wayfire_ipc() ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

static gboolean on_socket_event(gint fd, GIOCondition cond, gpointer data) {
    (void)fd; (void)data;
    if (!s_connection) return G_SOURCE_REMOVE;

    if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        disconnect_wayfire_ipc();
        if (!s_reconnect_id)
            s_reconnect_id = g_timeout_add(1000, schedule_reconnect, NULL);
        return G_SOURCE_REMOVE;
    }

    GSocket *sock  = g_socket_connection_get_socket(s_connection);
    gchar    chunk[4096];
    while (TRUE) {
        GError *error = NULL;
        gssize  bytes = g_socket_receive(sock, chunk, sizeof(chunk), NULL, &error);
        if (bytes > 0) {
            g_byte_array_append(s_buffer, (const guint8 *)chunk, (guint)bytes);
            continue;
        }
        if (bytes == 0) {
            disconnect_wayfire_ipc();
            if (!s_reconnect_id)
                s_reconnect_id = g_timeout_add(1000, schedule_reconnect, NULL);
            return G_SOURCE_REMOVE;
        }
        if (error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
            g_error_free(error);
            break;
        }
        g_warning("[keyboard-layout] Read error: %s",
                  error ? error->message : "unknown");
        if (error) g_error_free(error);
        disconnect_wayfire_ipc();
        if (!s_reconnect_id)
            s_reconnect_id = g_timeout_add(1000, schedule_reconnect, NULL);
        return G_SOURCE_REMOVE;
    }

    process_buffer();
    return G_SOURCE_CONTINUE;
}

/* Find WAYFIRE_SOCKET, falling back to scanning XDG_RUNTIME_DIR */
static gchar *resolve_socket_path(void) {
    const char *env = g_getenv("WAYFIRE_SOCKET");
    if (env && env[0]) return g_strdup(env);

    const char *runtime = g_getenv("XDG_RUNTIME_DIR");
    if (!runtime) return NULL;

    GError *error = NULL;
    GDir   *dir   = g_dir_open(runtime, 0, &error);
    if (!dir) {
        if (error) g_error_free(error);
        return NULL;
    }
    const gchar *name;
    gchar       *found = NULL;
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

static gboolean connect_wayfire_ipc(void) {
    if (s_connection) return TRUE;

    g_autofree gchar *socket_path = resolve_socket_path();
    if (!socket_path || !socket_path[0]) {
        if (!s_socket_warned) {
            g_warning("[keyboard-layout] WAYFIRE_SOCKET not found; layout indicator disabled");
            s_socket_warned = TRUE;
        }
        return FALSE;
    }

    GError          *error   = NULL;
    GSocketClient   *client  = g_socket_client_new();
    GSocketAddress  *address = g_unix_socket_address_new(socket_path);
    GSocketConnection *conn  = g_socket_client_connect(
        client, G_SOCKET_CONNECTABLE(address), NULL, &error);
    g_object_unref(address);
    g_object_unref(client);

    if (!conn) {
        if (error) { g_warning("[keyboard-layout] Connect failed: %s", error->message);
                     g_error_free(error); }
        return FALSE;
    }

    GSocket *sock = g_socket_connection_get_socket(conn);
    g_socket_set_blocking(sock, FALSE);

    s_connection    = conn;
    s_input_stream  = g_io_stream_get_input_stream(G_IO_STREAM(conn));
    s_output_stream = g_io_stream_get_output_stream(G_IO_STREAM(conn));
    s_buffer        = g_byte_array_new();
    s_socket_warned = FALSE;

    s_io_watch_id = g_unix_fd_add(g_socket_get_fd(sock),
        G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
        on_socket_event, NULL);

    /* subscribe to keyboard events and request initial state */
    send_request(
        "{\"method\":\"window-rules/events/watch\","
        "\"events\":[\"keyboard-modifier-state-changed\"]}");
    send_request("{\"method\":\"wayfire/get-keyboard-state\"}");

    return TRUE;
}

/* ── GTK helpers ─────────────────────────────────────────────────────────── */

static void update_all_widgets(void) {
    for (GList *l = s_widgets; l; l = l->next) {
        KeyboardLayoutWidget *w = l->data;
        if (w && GTK_IS_LABEL(w->label))
            gtk_label_set_text(GTK_LABEL(w->label), s_current_layout);
    }
}

static void on_widget_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    KeyboardLayoutWidget *w = user_data;
    s_widgets = g_list_remove(s_widgets, w);
    g_free(w);
}

/* ── public API ──────────────────────────────────────────────────────────── */

GtkWidget *create_keyboard_layout_widget(void) {
    /* connect once on first widget creation */
    if (!s_connection && !s_reconnect_id)
        if (!connect_wayfire_ipc())
            s_reconnect_id = g_timeout_add(1000, schedule_reconnect, NULL);

    KeyboardLayoutWidget *w = g_new0(KeyboardLayoutWidget, 1);
    w->box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    w->label = gtk_label_new(s_current_layout);

    gtk_widget_set_name(w->box,   "keyboard-layout-box");
    gtk_widget_set_name(w->label, "keyboard-layout-label");
    gtk_widget_set_halign(w->box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(w->box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(w->box, 10);

    gtk_container_add(GTK_CONTAINER(w->box), w->label);
    s_widgets = g_list_prepend(s_widgets, w);

    g_signal_connect(w->box, "destroy", G_CALLBACK(on_widget_destroy), w);
    return w->box;
}
