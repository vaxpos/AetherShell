#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include <glib-unix.h>
#include <json-glib/json-glib.h>
#include <gio/gunixsocketaddress.h>
#if defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#endif
#include <xkbcommon/xkbregistry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <math.h>

// PulseAudio
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

// إعدادات المظهر
#define OSD_WIDTH 200
#define OSD_HEIGHT 200
#define FONT_SIZE 40.0
#define SHOW_DURATION_MS 1500
#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

// أنواع OSD
typedef enum {
    OSD_KEYBOARD,
    OSD_VOLUME,
    OSD_BRIGHTNESS
} OsdType;

// المتغيرات العامة
GtkWidget *window = NULL;
GtkWidget *drawing_area = NULL;
guint hide_timer_id = 0;
gboolean use_wayland_layer_shell = FALSE;
gboolean wayfire_socket_warned = FALSE;
struct rxkb_context *rxkb_ctx = NULL;
GSocketConnection *wayfire_connection = NULL;
GInputStream *wayfire_input = NULL;
GOutputStream *wayfire_output = NULL;
GByteArray *wayfire_buffer = NULL;
guint wayfire_io_watch_id = 0;
guint wayfire_reconnect_id = 0;

// حالة OSD الحالية
OsdType current_osd_type = OSD_KEYBOARD;
char current_text[32] = "";
int current_volume = -1;
int is_muted = -1;
int current_brightness = 0;

// PulseAudio
pa_glib_mainloop *my_pa_mainloop = NULL;
pa_context *pa_ctx = NULL;
uint32_t default_sink_index = PA_INVALID_INDEX;

// مصفوفة لتخزين اللغات
char loaded_layouts[8][16];
int loaded_layouts_count = 0;
int current_layout_index = -1;

// تعريف الدوال المسبق
void show_osd();
void to_upper_str(char *str);
static void normalize_layout_name(char *layout);
static const char *brief_layout_name(const char *layout_name);
static const char *display_layout_name(const char *layout_name);
static gboolean is_wayfire_socket_candidate(const char *path, const char *name);
static gchar *find_wayfire_socket_in_dir(const char *dir_path, int depth);
static gchar *resolve_wayfire_socket_path(void);
static gboolean connect_wayfire_ipc(void);
static void disconnect_wayfire_ipc(void);
static gboolean schedule_wayfire_reconnect(gpointer data);
static gboolean on_wayfire_socket_event(gint fd, GIOCondition condition, gpointer data);
static gboolean handle_wayfire_message(JsonObject *obj, gboolean show_popup);
static gboolean process_wayfire_buffer(gboolean show_popup);
static gboolean send_wayfire_request(const char *json);
static void update_layouts_from_json_array(JsonArray *layouts);
static void setup_keyboard_monitoring(void);
static gboolean is_wayland_session(void);
static void update_osd_position(int width, int height);
static void apply_input_passthrough(void);

// ----------------------------------------------------------------------
// PulseAudio Handlers
// ----------------------------------------------------------------------

static gboolean get_overamplification(void) {
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!conn) {
        if (error) g_error_free(error);
        return FALSE;
    }
    
    GVariant *result = g_dbus_connection_call_sync(
        conn,
        "org.venom.Audio",
        "/org/venom/Audio",
        "org.venom.Audio",
        "GetOveramplification",
        NULL,
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        NULL,
        &error
    );
    
    gboolean is_over = FALSE;
    if (result) {
        g_variant_get(result, "(b)", &is_over);
        g_variant_unref(result);
    } else if (error) {
        g_error_free(error);
    }
    g_object_unref(conn);
    return is_over;
}

int current_max_volume = 100;

void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void)c;
    (void)userdata;
    if (eol > 0 || !i) return;

    pa_cvolume cv = i->volume;
    int vol = (pa_cvolume_avg(&cv) * 100) / PA_VOLUME_NORM;
    
    gboolean over_amp = get_overamplification();
    current_max_volume = over_amp ? 150 : 100;
    
    if (vol > current_max_volume) vol = current_max_volume;
    
    int changed = 0;
    if (current_volume != -1 && (current_volume != vol || is_muted != i->mute)) {
        changed = 1;
    }
    
    current_volume = vol;
    is_muted = i->mute;
    
    if (changed) {
        current_osd_type = OSD_VOLUME;
        printf("[PA] Sink volume updated: %d%%, muted: %d\n", vol, is_muted);
        show_osd();
    }
}

void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata) {
    (void)userdata;
    if (!i) return;
    printf("[PA] Default sink is %s\n", i->default_sink_name);
    pa_context_get_sink_info_by_name(c, i->default_sink_name, sink_info_cb, NULL);
}

void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *userdata) {
    (void)index;
    (void)userdata;
    if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_CHANGE &&
        (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK) {
        printf("[PA] Sink event received, fetching info...\n");
        pa_context_get_server_info(c, server_info_cb, NULL);
    }
}

void context_state_cb(pa_context *c, void *userdata) {
    (void)userdata;
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            printf("[PA] Context Ready\n");
            pa_context_get_server_info(c, server_info_cb, NULL);
            pa_context_set_subscribe_callback(c, subscribe_cb, NULL);
            pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
            break;
        case PA_CONTEXT_FAILED:
            printf("[PA] Context Failed\n");
            break;
        case PA_CONTEXT_TERMINATED:
            printf("[PA] Context Terminated\n");
            break;
        default:
            printf("[PA] Context State Changed: %d\n", pa_context_get_state(c));
            break;
    }
}

void setup_pulseaudio() {
    my_pa_mainloop = pa_glib_mainloop_new(NULL);
    pa_mainloop_api *api = pa_glib_mainloop_get_api(my_pa_mainloop);
    pa_ctx = pa_context_new(api, "Venom OSD");
    
    pa_context_set_state_callback(pa_ctx, context_state_cb, NULL);
    pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
}

// ----------------------------------------------------------------------
// Utils
// ----------------------------------------------------------------------
void to_upper_str(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = toupper((unsigned char)str[i]);
    }
}

static void normalize_layout_name(char *layout) {
    if (!layout || !layout[0]) return;

    to_upper_str(layout);
    if (strcmp(layout, "ARA") == 0 || strcmp(layout, "IQ") == 0) {
        strcpy(layout, "AR");
    } else if (strcmp(layout, "USA") == 0) {
        strcpy(layout, "US");
    }
}

static const char *brief_layout_name(const char *layout_name) {
    if (!layout_name || !layout_name[0]) return NULL;

    if (!rxkb_ctx) {
        rxkb_ctx = rxkb_context_new(RXKB_CONTEXT_NO_FLAGS);
        if (rxkb_ctx) {
            rxkb_context_parse_default_ruleset(rxkb_ctx);
        }
    }

    if (!rxkb_ctx) return NULL;

    struct rxkb_layout *layout = rxkb_layout_first(rxkb_ctx);
    for (; layout != NULL; layout = rxkb_layout_next(layout)) {
        const char *name = rxkb_layout_get_name(layout);
        const char *description = rxkb_layout_get_description(layout);
        if ((name && g_strcmp0(name, layout_name) == 0) ||
            (description && g_strcmp0(description, layout_name) == 0)) {
            return rxkb_layout_get_brief(layout);
        }
    }

    return NULL;
}

static const char *display_layout_name(const char *layout_name) {
    if (!layout_name || !layout_name[0]) return NULL;

    if (g_ascii_strcasecmp(layout_name, "AR") == 0 ||
        g_ascii_strcasecmp(layout_name, "ARA") == 0 ||
        g_ascii_strcasecmp(layout_name, "IQ") == 0 ||
        g_ascii_strcasecmp(layout_name, "Arabic") == 0) {
        return "Arabic";
    }

    if (g_ascii_strcasecmp(layout_name, "US") == 0 ||
        g_ascii_strcasecmp(layout_name, "USA") == 0 ||
        g_ascii_strcasecmp(layout_name, "EN") == 0 ||
        g_ascii_strcasecmp(layout_name, "English") == 0) {
        return "English";
    }

    return layout_name;
}

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

static void setup_keyboard_monitoring(void) {
    if (!connect_wayfire_ipc() && !wayfire_reconnect_id) {
        wayfire_reconnect_id = g_timeout_add(1000, schedule_wayfire_reconnect, NULL);
    }
}

static void update_layouts_from_json_array(JsonArray *layouts) {
    if (!layouts) return;

    memset(loaded_layouts, 0, sizeof(loaded_layouts));
    loaded_layouts_count = 0;

    guint n_layouts = json_array_get_length(layouts);
    for (guint i = 0; i < n_layouts && loaded_layouts_count < 8; ++i) {
        const char *layout_name = json_array_get_string_element(layouts, i);
        if (!layout_name || !layout_name[0]) continue;

        const char *brief = brief_layout_name(layout_name);
        const char *display_name = display_layout_name(brief && brief[0] ? brief : layout_name);
        if (display_name && display_name[0]) {
            g_strlcpy(loaded_layouts[loaded_layouts_count], display_name, sizeof(loaded_layouts[0]));
        } else {
            g_strlcpy(loaded_layouts[loaded_layouts_count], layout_name, sizeof(loaded_layouts[0]));
        }

        normalize_layout_name(loaded_layouts[loaded_layouts_count]);
        loaded_layouts_count++;
    }
}

static gboolean handle_wayfire_message(JsonObject *obj, gboolean show_popup) {
    if (!obj) return FALSE;
    if (json_object_has_member(obj, "error")) return FALSE;

    JsonObject *state = obj;
    if (json_object_has_member(obj, "event")) {
        const char *event_name = json_object_get_string_member(obj, "event");
        if (!g_strcmp0(event_name, "keyboard-modifier-state-changed")) {
            if (!json_object_has_member(obj, "state")) return FALSE;
            state = json_object_get_object_member(obj, "state");
        } else {
            return FALSE;
        }
    }

    if (!json_object_has_member(state, "possible-layouts") ||
        !json_object_has_member(state, "layout-index")) {
        return FALSE;
    }

    JsonArray *layouts = json_object_get_array_member(state, "possible-layouts");
    update_layouts_from_json_array(layouts);
    if (loaded_layouts_count <= 0) return FALSE;

    int current = json_object_get_int_member(state, "layout-index");
    if (current < 0 || current >= loaded_layouts_count) return FALSE;

    if (current == current_layout_index &&
        g_strcmp0(current_text, loaded_layouts[current]) == 0) {
        return TRUE;
    }

    current_layout_index = current;
    g_strlcpy(current_text, loaded_layouts[current_layout_index], sizeof(current_text));
    current_osd_type = OSD_KEYBOARD;

    if (show_popup) {
        show_osd();
    }

    return TRUE;
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

// ----------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------
void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double radius) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - radius, y + radius, radius, -M_PI/2, 0);
    cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI/2);
    cairo_arc(cr, x + radius, y + h - radius, radius, M_PI/2, M_PI);
    cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3*M_PI/2);
    cairo_close_path(cr);
}

void draw_icon_speaker(cairo_t *cr, double cx, double cy, double size, int muted) {
    // رسم مكبر صوت بسيط
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 4.0);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    
    // هيكل المكبر
    cairo_move_to(cr, cx - size*0.4, cy - size*0.2);
    cairo_line_to(cr, cx - size*0.1, cy - size*0.2);
    cairo_line_to(cr, cx + size*0.2, cy - size*0.4);
    cairo_line_to(cr, cx + size*0.2, cy + size*0.4);
    cairo_line_to(cr, cx - size*0.1, cy + size*0.2);
    cairo_line_to(cr, cx - size*0.4, cy + size*0.2);
    cairo_close_path(cr);
    cairo_stroke_preserve(cr);
    cairo_fill(cr);

    if (muted) {
        cairo_set_source_rgb(cr, 1.0, 0.2, 0.2);
        cairo_move_to(cr, cx + size*0.1, cy - size*0.2);
        cairo_line_to(cr, cx + size*0.5, cy + size*0.2);
        cairo_move_to(cr, cx + size*0.5, cy - size*0.2);
        cairo_line_to(cr, cx + size*0.1, cy + size*0.2);
        cairo_stroke(cr);
    } else {
        // رسم أمواج الصوت
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_arc(cr, cx + size*0.2, cy, size*0.2, -M_PI/4, M_PI/4);
        cairo_stroke(cr);
        cairo_arc(cr, cx + size*0.2, cy, size*0.4, -M_PI/4, M_PI/4);
        cairo_stroke(cr);
    }
}

void draw_icon_sun(cairo_t *cr, double cx, double cy, double size) {
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 4.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    
    // دائرة الشمس
    cairo_arc(cr, cx, cy, size*0.25, 0, 2*M_PI);
    cairo_stroke_preserve(cr);
    cairo_fill(cr);
    
    // أشعة الشمس
    for (int i = 0; i < 8; ++i) {
        double angle = i * M_PI / 4.0;
        cairo_move_to(cr, cx + cos(angle) * size * 0.35, cy + sin(angle) * size * 0.35);
        cairo_line_to(cr, cx + cos(angle) * size * 0.5, cy + sin(angle) * size * 0.5);
        cairo_stroke(cr);
    }
}

gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)widget; (void)data;
    gint width = gtk_widget_get_allocated_width(widget);
    gint height = gtk_widget_get_allocated_height(widget);
    if (width <= 0) width = OSD_WIDTH;
    if (height <= 0) height = OSD_HEIGHT;
    
    // خلفية شفافة
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    double x = 10, y = 10, w = width - 20, h = height - 20;
    double radius = 20.0;

    // رسم الخلفية
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.0);
    draw_rounded_rect(cr, x, y, w, h, radius);
    cairo_fill(cr);

    if (current_osd_type == OSD_KEYBOARD) {
        cairo_set_source_rgb(cr, 0.0, 1.0, 1.0);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, FONT_SIZE);

        cairo_text_extents_t extents;
        cairo_text_extents(cr, current_text, &extents);

        double text_x = (width - extents.width) / 2.0 - extents.x_bearing;
        double text_y = (height - extents.height) / 2.0 - extents.y_bearing;

        cairo_move_to(cr, text_x, text_y);
        cairo_show_text(cr, current_text);
    } 
    else if (current_osd_type == OSD_VOLUME || current_osd_type == OSD_BRIGHTNESS) {
        int percentage = (current_osd_type == OSD_VOLUME) ? current_volume : current_brightness;
        
        // رسم الأيقونة في النصف العلوي
        double icon_cx = width / 2.0;
        double icon_cy = height / 2.0 - 20;
        
        if (current_osd_type == OSD_VOLUME) {
            draw_icon_speaker(cr, icon_cx, icon_cy, 60.0, is_muted);
        } else {
            draw_icon_sun(cr, icon_cx, icon_cy, 60.0);
        }

        // رسم شريط التقدم في النصف السفلي
        double bar_w = w * 0.8;
        double bar_h = 10.0;
        double bar_x = (width - bar_w) / 2.0;
        double bar_y = y + h - 30.0;
        
        // خلفية الشريط
        draw_rounded_rect(cr, bar_x, bar_y, bar_w, bar_h, bar_h/2.0);
        cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 1.0);
        cairo_fill(cr);
        
        // مستوى الشريط
        if (percentage > 0) {
            double max_val = (current_osd_type == OSD_VOLUME) ? (double)current_max_volume : 100.0;
            double fill_ratio = percentage / max_val;
            if (fill_ratio > 1.0) fill_ratio = 1.0;
            
            double fill_w = bar_w * fill_ratio;
            draw_rounded_rect(cr, bar_x, bar_y, fill_w, bar_h, bar_h/2.0);
            if (current_osd_type == OSD_VOLUME && is_muted) {
                cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
            } else {
                cairo_set_source_rgb(cr, 0.0, 1.0, 1.0);
            }
            cairo_fill(cr);
        }
        
        // رسم النسبة المئوية
        char val_str[16];
        snprintf(val_str, sizeof(val_str), "%d%%", percentage);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 16.0);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_text_extents_t extents;
        cairo_text_extents(cr, val_str, &extents);
        cairo_move_to(cr, (width - extents.width)/2.0 - extents.x_bearing, bar_y - 10.0);
        cairo_show_text(cr, val_str);
    }
    
    return FALSE;
}

gboolean hide_osd_cb(gpointer data) {
    (void)data;
    gtk_widget_hide(window);
    hide_timer_id = 0;
    return G_SOURCE_REMOVE;
}

void show_osd() {
    printf("[OSD] Showing OSD Type: %d\n", current_osd_type);
    if (hide_timer_id) {
        g_source_remove(hide_timer_id);
    }
    
    // Resize for volume/brightness since they need more height
    int width = 200;
    int height = 200;
    if (current_osd_type == OSD_KEYBOARD) {
        height = 100;
    } else {
        height = 200;
    }

    gtk_widget_set_size_request(drawing_area, width, height);
    gtk_window_resize(GTK_WINDOW(window), width, height);
    update_osd_position(width, height);
    
    gtk_widget_queue_draw(drawing_area);
    gtk_widget_show_all(window);
    apply_input_passthrough();
    
    hide_timer_id = g_timeout_add(SHOW_DURATION_MS, hide_osd_cb, NULL);
}

gboolean on_inotify(GIOChannel *source, GIOCondition cond, gpointer data) {
    (void)cond; (void)data;
    char buf[BUF_LEN];
    gsize bytes_read;
    g_io_channel_read_chars(source, buf, BUF_LEN, &bytes_read, NULL);
    
    if (bytes_read > 0) {
        struct inotify_event *event = (struct inotify_event *)buf;
        if ((event->mask & IN_MODIFY) && (strcmp(event->name, "brightness") == 0 || event->len == 0)) {
            // Read brightness
            printf("[Brightness] Modified event received\n");
            FILE *f = fopen("/sys/class/backlight/amd_backlight/brightness", "r");
            FILE *fm = fopen("/sys/class/backlight/amd_backlight/max_brightness", "r");
            if (!f) f = fopen("/sys/class/backlight/intel_backlight/brightness", "r");
            if (!fm) fm = fopen("/sys/class/backlight/intel_backlight/max_brightness", "r");
            
            if (f && fm) {
                int b = 0, max_b = 1;
                if (fscanf(f, "%d", &b) == 1 && fscanf(fm, "%d", &max_b) == 1) {
                    current_brightness = (b * 100) / max_b;
                    current_osd_type = OSD_BRIGHTNESS;
                    printf("[Brightness] Level %d%%\n", current_brightness);
                    show_osd();
                }
                if (f) fclose(f);
                if (fm) fclose(fm);
            } else {
                printf("[Brightness] Could not open sysfs brightness files\n");
            }
        }
    }
    return TRUE;
}

// ----------------------------------------------------------------------
// Setups
// ----------------------------------------------------------------------
static gboolean is_wayland_session(void) {
#if defined(GDK_WINDOWING_WAYLAND)
    return GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default());
#else
    return FALSE;
#endif
}

static void update_osd_position(int width, int height) {
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = display ? gdk_display_get_primary_monitor(display) : NULL;
    if (!monitor) return;

    GdkRectangle workarea;
    gdk_monitor_get_workarea(monitor, &workarea);

    int x = workarea.x + (workarea.width - width) / 2;
    int y = workarea.y + (workarea.height - height) / 2;

    if (use_wayland_layer_shell) {
        gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
        gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
        gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, x);
        gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, y);
        gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
        gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
        gtk_layer_set_monitor(GTK_WINDOW(window), monitor);
    } else {
        gtk_window_move(GTK_WINDOW(window), x, y);
    }
}

static void apply_input_passthrough(void) {
    GdkWindow *gdk_win = gtk_widget_get_window(window);
    if (!gdk_win) return;

    gdk_window_set_pass_through(gdk_win, TRUE);

    cairo_region_t *empty = cairo_region_create();
    gdk_window_input_shape_combine_region(gdk_win, empty, 0, 0);
    cairo_region_destroy(empty);

#if defined(GDK_WINDOWING_WAYLAND)
    if (is_wayland_session() && GDK_IS_WAYLAND_WINDOW(gdk_win)) {
        GdkDisplay *display = gdk_display_get_default();
        struct wl_compositor *compositor = display ? gdk_wayland_display_get_wl_compositor(display) : NULL;
        struct wl_surface *surface = gdk_wayland_window_get_wl_surface(gdk_win);
        if (compositor && surface) {
            struct wl_region *region = wl_compositor_create_region(compositor);
            wl_surface_set_input_region(surface, region);
            wl_surface_commit(surface);
            wl_region_destroy(region);
        }
    }
#endif
}

void setup_window() {
    window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_default_size(GTK_WINDOW(window), OSD_WIDTH, OSD_HEIGHT);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
    gtk_widget_set_app_paintable(window, TRUE);

    use_wayland_layer_shell = is_wayland_session() && gtk_layer_is_supported();
    if (use_wayland_layer_shell) {
        gtk_layer_init_for_window(GTK_WINDOW(window));
        gtk_layer_set_namespace(GTK_WINDOW(window), "venom-osd");
        gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_set_exclusive_zone(GTK_WINDOW(window), 0);
    } else {
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    }
    
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) {
        gtk_widget_set_visual(window, visual);
    }
    
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, OSD_WIDTH, OSD_HEIGHT);
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    gtk_container_add(GTK_CONTAINER(window), drawing_area);
    
    gtk_widget_realize(window);
    apply_input_passthrough();
}

// ----------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------
#include "osd.h"

void osd_init(void) {
    setup_window();
    setup_pulseaudio();
    setup_keyboard_monitoring();
    
    int inotifyFd = inotify_init();
    if (inotifyFd >= 0) {
        // Find backlight dir and watch it (amd_backlight, amdgpu_bl0, etc)
        // For simplicity, we assume amd_backlight or similar. A real implementation
        // might loop through /sys/class/backlight/*/.
        inotify_add_watch(inotifyFd, "/sys/class/backlight/amd_backlight", IN_MODIFY);
        inotify_add_watch(inotifyFd, "/sys/class/backlight/amdgpu_bl0", IN_MODIFY);
        inotify_add_watch(inotifyFd, "/sys/class/backlight/amdgpu_bl1", IN_MODIFY);
        inotify_add_watch(inotifyFd, "/sys/class/backlight/acpi_video0", IN_MODIFY);

        GIOChannel *channel = g_io_channel_unix_new(inotifyFd);
        g_io_add_watch(channel, G_IO_IN, on_inotify, NULL);
    }
}
