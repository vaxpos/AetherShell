/*
 * workspaces.c — Workspace switcher widget for the Aether panel
 *
 * Uses the Wayfire IPC socket (WAYFIRE_SOCKET env var) to query and switch
 * workspaces, matching the implementation used in vaxpwy-panel.
 *
 * Dots are rendered via Cairo on GtkDrawingArea+GtkEventBox for pixel-perfect
 * circular/pill shapes — bypassing GTK button internal padding entirely.
 *   • Inactive workspace → small filled circle
 *   • Active  workspace → wider pill/capsule shape (purple)
 */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <stdint.h>
#include "workspaces.h"

#define DOT_H        10    /* dot / pill height in pixels        */
#define DOT_W        10    /* inactive dot width (circle)        */
#define PILL_W      30    /* active workspace pill width        */
#define DOT_MARGIN   4    /* horizontal gap between indicators  */

/* ── per-widget state ───────────────────────────────────────────────────── */
typedef struct {
    GtkWidget *box;
    int current_workspace;
    int workspace_count;
    int grid_width;
    int grid_height;
    int output_id;
    guint timer_id;
} WorkspaceData;

/* per-dot drawing context */
typedef struct {
    WorkspaceData *ws_data;
    int            index;
    gboolean       is_active;
} DotCtx;

/* ── forward declaration ────────────────────────────────────────────────── */
static void switch_workspace(WorkspaceData *data, int workspace_idx);

/* ── Wayfire IPC helpers ─────────────────────────────────────────────────── */

static gboolean wayfire_ipc_call(const char *request, JsonParser **out_parser) {
    const char *socket_path = g_getenv("WAYFIRE_SOCKET");
    if (!socket_path || !*socket_path)
        return FALSE;

    g_autoptr(GError) error = NULL;
    g_autoptr(GSocketClient) client = g_socket_client_new();
    g_autoptr(GSocketConnectable) address = G_SOCKET_CONNECTABLE(
        g_unix_socket_address_new(socket_path));
    g_autoptr(GSocketConnection) connection =
        g_socket_client_connect(client, address, NULL, &error);
    if (!connection) {
        g_warning("Failed to connect to WAYFIRE_SOCKET: %s", error->message);
        return FALSE;
    }

    GOutputStream *output = g_io_stream_get_output_stream(G_IO_STREAM(connection));
    GInputStream  *input  = g_io_stream_get_input_stream(G_IO_STREAM(connection));

    guint32 length = (guint32)strlen(request);
    gsize written = 0;
    if (!g_output_stream_write_all(output, &length, sizeof(length), &written, NULL, &error) ||
        written != sizeof(length)) {
        g_warning("Failed to write Wayfire IPC header: %s",
                  error ? error->message : "short write");
        return FALSE;
    }

    written = 0;
    if (!g_output_stream_write_all(output, request, length, &written, NULL, &error) ||
        written != length) {
        g_warning("Failed to write Wayfire IPC payload: %s",
                  error ? error->message : "short write");
        return FALSE;
    }

    guint32 response_length = 0;
    gsize read_bytes = 0;
    if (!g_input_stream_read_all(input, &response_length, sizeof(response_length),
                                 &read_bytes, NULL, &error) ||
        read_bytes != sizeof(response_length)) {
        g_warning("Failed to read Wayfire IPC response header: %s",
                  error ? error->message : "short read");
        return FALSE;
    }

    g_autofree gchar *response = g_malloc0(response_length + 1);
    read_bytes = 0;
    if (!g_input_stream_read_all(input, response, response_length, &read_bytes, NULL, &error) ||
        read_bytes != response_length) {
        g_warning("Failed to read Wayfire IPC response payload: %s",
                  error ? error->message : "short read");
        return FALSE;
    }

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, response, response_length, &error)) {
        g_warning("Failed to parse Wayfire IPC response: %s", error->message);
        g_object_unref(parser);
        return FALSE;
    }

    *out_parser = parser;
    return TRUE;
}

static JsonObject *get_workspace_object(JsonNode *node) {
    if (!JSON_NODE_HOLDS_OBJECT(node))
        return NULL;
    JsonObject *root = json_node_get_object(node);
    if (json_object_has_member(root, "error"))
        return NULL;
    return root;
}

static JsonObject *find_current_wset(JsonArray *array, int preferred_output_id) {
    guint length = json_array_get_length(array);
    JsonObject *fallback = NULL;

    for (guint i = 0; i < length; i++) {
        JsonObject *item = json_array_get_object_element(array, i);
        if (!item || !json_object_has_member(item, "workspace"))
            continue;
        if (!fallback)
            fallback = item;
        if (preferred_output_id >= 0 &&
            json_object_get_int_member(item, "output-id") == preferred_output_id)
            return item;
    }
    return fallback;
}

/* ── Cairo drawing ───────────────────────────────────────────────────────── */

/* Draw a pill / capsule with full rounded ends */
static void cairo_pill(cairo_t *cr, double x, double y, double w, double h) {
    double r = h / 2.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + r,     y + r, r,  M_PI / 2.0, 3.0 * M_PI / 2.0);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0,       M_PI / 2.0);
    cairo_close_path(cr);
}

static gboolean on_dot_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    DotCtx *dot = user_data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    if (dot->is_active) {
        /* purple pill */
        cairo_set_source_rgba(cr, 0.486, 0.227, 0.929, 1.0);   /* #00fcd2 */
        cairo_pill(cr, 0, 0, w, h);
        cairo_fill(cr);
    } else {
        /* white circle */
        double cx = w / 2.0, cy = h / 2.0, r = MIN(cx, cy);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
        cairo_fill(cr);
    }
    return FALSE;
}

/* Draw workspaces background */
static gboolean on_container_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)user_data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.3);
    cairo_pill(cr, 0, 0, w, h);
    cairo_fill(cr);

    return FALSE; /* allow children to draw */
}

/* ── Click handling ──────────────────────────────────────────────────────── */

static gboolean on_dot_click(GtkWidget *widget, GdkEventButton *event,
                              gpointer user_data) {
    (void)widget; (void)event;
    DotCtx *dot = user_data;
    if (dot->index != dot->ws_data->current_workspace)
        switch_workspace(dot->ws_data, dot->index);
    return GDK_EVENT_STOP;
}

/* ── GTK widget rebuild ──────────────────────────────────────────────────── */

static void rebuild_workspace_buttons(WorkspaceData *data) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(data->box));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    for (int i = 0; i < data->workspace_count; i++) {
        gboolean is_active = (i == data->current_workspace);

        DotCtx *dot = g_new0(DotCtx, 1);
        dot->ws_data  = data;
        dot->index    = i;
        dot->is_active = is_active;

        /* EventBox catches clicks */
        GtkWidget *ev = gtk_event_box_new();
        /* Do NOT set visible_window to FALSE, otherwise we miss hits on transparent areas! */
        gtk_widget_add_events(ev, GDK_BUTTON_PRESS_MASK);
        gtk_widget_set_valign(ev, GTK_ALIGN_CENTER);
        
        /* Set CSS to make EventBox fully transparent so it doesn't draw an ugly background */
        GtkStyleContext *ev_ctx = gtk_widget_get_style_context(ev);
        gtk_style_context_add_class(ev_ctx, "workspace-dot-event-box");

        g_signal_connect(ev, "button-press-event", G_CALLBACK(on_dot_click), dot);
        g_signal_connect_swapped(ev, "destroy", G_CALLBACK(g_free), dot);

        /* DrawingArea renders the dot/pill via Cairo */
        GtkWidget *da = gtk_drawing_area_new();
        gtk_widget_set_app_paintable(da, TRUE);
        gtk_widget_set_size_request(da, is_active ? PILL_W : DOT_W, DOT_H);
        gtk_widget_set_valign(da, GTK_ALIGN_CENTER);
        g_signal_connect(da, "draw", G_CALLBACK(on_dot_draw), dot);

        gtk_container_add(GTK_CONTAINER(ev), da);
        gtk_box_pack_start(GTK_BOX(data->box), ev, FALSE, FALSE, DOT_MARGIN / 2);
    }

    gtk_widget_show_all(data->box);
}

/* ── Workspace refresh (polls Wayfire IPC) ───────────────────────────────── */

static gboolean refresh_workspaces(gpointer user_data) {
    WorkspaceData *data = (WorkspaceData *)user_data;
    if (!GTK_IS_WIDGET(data->box))
        return G_SOURCE_REMOVE;

    g_autoptr(JsonParser) parser = NULL;
    if (!wayfire_ipc_call("{\"method\":\"window-rules/list-wsets\"}", &parser))
        return G_SOURCE_CONTINUE;

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_ARRAY(root))
        return G_SOURCE_CONTINUE;

    JsonArray  *array = json_node_get_array(root);
    JsonObject *wset  = find_current_wset(array, data->output_id);
    if (!wset)
        return G_SOURCE_CONTINUE;

    JsonObject *workspace = json_object_get_object_member(wset, "workspace");
    if (!workspace)
        return G_SOURCE_CONTINUE;

    data->output_id = json_object_get_int_member(wset, "output-id");

    int grid_width        = json_object_get_int_member(workspace, "grid_width");
    int grid_height       = json_object_get_int_member(workspace, "grid_height");
    int current_x         = json_object_get_int_member(workspace, "x");
    int current_y         = json_object_get_int_member(workspace, "y");
    int current_workspace = current_y * grid_width + current_x;
    int workspace_count   = grid_width * grid_height;

    if (grid_width <= 0 || grid_height <= 0)
        return G_SOURCE_CONTINUE;

    if (grid_width        != data->grid_width        ||
        grid_height       != data->grid_height       ||
        current_workspace != data->current_workspace ||
        workspace_count   != data->workspace_count) {
        data->grid_width        = grid_width;
        data->grid_height       = grid_height;
        data->workspace_count   = workspace_count;
        data->current_workspace = current_workspace;
        rebuild_workspace_buttons(data);
    }

    return G_SOURCE_CONTINUE;
}

/* ── Workspace switch ────────────────────────────────────────────────────── */

static void switch_workspace(WorkspaceData *data, int workspace_idx) {
    if (data->output_id < 0 || data->grid_width <= 0)
        return;

    int target_x = workspace_idx % data->grid_width;
    int target_y = workspace_idx / data->grid_width;

    char request[256];
    g_snprintf(request, sizeof(request),
        "{\"method\":\"vswitch/set-workspace\",\"data\":{\"x\":%d,\"y\":%d,\"output-id\":%d}}",
        target_x, target_y, data->output_id);

    g_autoptr(JsonParser) parser = NULL;
    if (!wayfire_ipc_call(request, &parser))
        return;

    JsonObject *response = get_workspace_object(json_parser_get_root(parser));
    if (!response) {
        g_warning("Wayfire rejected workspace switch request. "
                  "Ensure ipc, ipc-rules and vswitch plugins are enabled.");
        return;
    }

    data->current_workspace = workspace_idx;
    rebuild_workspace_buttons(data);
}

static void on_widget_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    WorkspaceData *data = (WorkspaceData *)user_data;
    if (data->timer_id > 0)
        g_source_remove(data->timer_id);
    g_free(data);
}

/* ── public API ──────────────────────────────────────────────────────────── */

GtkWidget *create_workspaces_widget(void) {
    WorkspaceData *data        = g_new0(WorkspaceData, 1);
    
    GtkWidget *event_box = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box), TRUE);
    gtk_widget_set_app_paintable(event_box, TRUE);
    gtk_widget_set_margin_top(event_box, 4);
    gtk_widget_set_margin_bottom(event_box, 4);
    g_signal_connect(event_box, "draw", G_CALLBACK(on_container_draw), NULL);

    data->box                  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    /* Internal padding for the background drawn by event_box */
    gtk_widget_set_margin_start(data->box, 8);
    gtk_widget_set_margin_end(data->box, 8);
    
    data->output_id            = -1;
    data->grid_width           = 3;
    data->grid_height          = 3;
    data->workspace_count      = 9;
    data->current_workspace    = 0;

    gtk_container_add(GTK_CONTAINER(event_box), data->box);

    rebuild_workspace_buttons(data);
    refresh_workspaces(data);
    data->timer_id = g_timeout_add(700, refresh_workspaces, data);
    g_signal_connect(event_box, "destroy", G_CALLBACK(on_widget_destroy), data);

    return event_box;
}
