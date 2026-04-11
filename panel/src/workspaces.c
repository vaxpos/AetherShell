/*
 * workspaces.c — Workspace switcher widget for the Aether panel
 */

#include <gtk/gtk.h>
#include <math.h>
#include "workspaces.h"
#include "compositor_backend.h"

#define DOT_H 10
#define DOT_W 10
#define PILL_W 30
#define DOT_MARGIN 4

typedef struct {
    GtkWidget *box;
    int current_workspace;
    int workspace_count;
    int grid_width;
    int grid_height;
    int output_id;
} WorkspaceData;

typedef struct {
    WorkspaceData *ws_data;
    int index;
    gboolean is_active;
} DotCtx;

static WorkspaceData *s_workspace_data = NULL;
static gboolean on_dot_click(GtkWidget *widget, GdkEventButton *event, gpointer user_data);

static void cairo_pill(cairo_t *cr, double x, double y, double w, double h) {
    double r = h / 2.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + r, y + r, r, M_PI / 2.0, 3.0 * M_PI / 2.0);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, M_PI / 2.0);
    cairo_close_path(cr);
}

static gboolean on_dot_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    DotCtx *dot = user_data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    (void)widget;

    if (dot->is_active) {
        cairo_set_source_rgba(cr, 0.486, 0.227, 0.929, 1.0);
        cairo_pill(cr, 0, 0, w, h);
        cairo_fill(cr);
    } else {
        double cx = w / 2.0;
        double cy = h / 2.0;
        double r = MIN(cx, cy);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
        cairo_fill(cr);
    }
    return FALSE;
}

static gboolean on_container_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    (void)user_data;

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.3);
    cairo_pill(cr, 0, 0, w, h);
    cairo_fill(cr);
    return FALSE;
}

static void rebuild_workspace_buttons(WorkspaceData *data) {
    GList *children;

    if (!data || !data->box) return;

    children = gtk_container_get_children(GTK_CONTAINER(data->box));
    for (GList *l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    for (int i = 0; i < data->workspace_count; i++) {
        DotCtx *dot = g_new0(DotCtx, 1);
        GtkWidget *ev = gtk_event_box_new();
        GtkWidget *da = gtk_drawing_area_new();

        dot->ws_data = data;
        dot->index = i;
        dot->is_active = (i == data->current_workspace);

        gtk_widget_add_events(ev, GDK_BUTTON_PRESS_MASK);
        gtk_widget_set_valign(ev, GTK_ALIGN_CENTER);
        gtk_style_context_add_class(gtk_widget_get_style_context(ev), "workspace-dot-event-box");
        g_signal_connect_swapped(ev, "destroy", G_CALLBACK(g_free), dot);

        g_signal_connect(ev, "button-press-event", G_CALLBACK(on_dot_click), dot);

        gtk_widget_set_app_paintable(da, TRUE);
        gtk_widget_set_size_request(da, dot->is_active ? PILL_W : DOT_W, DOT_H);
        gtk_widget_set_valign(da, GTK_ALIGN_CENTER);
        g_signal_connect(da, "draw", G_CALLBACK(on_dot_draw), dot);

        gtk_container_add(GTK_CONTAINER(ev), da);
        gtk_box_pack_start(GTK_BOX(data->box), ev, FALSE, FALSE, DOT_MARGIN / 2);
    }

    gtk_widget_show_all(data->box);
}

static gboolean on_dot_click(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    DotCtx *clicked = user_data;
    int target_x;
    int target_y;

    (void)widget;
    (void)event;

    if (clicked->index == clicked->ws_data->current_workspace) return GDK_EVENT_STOP;

    target_x = clicked->index % clicked->ws_data->grid_width;
    target_y = clicked->index / clicked->ws_data->grid_width;
    panel_compositor_backend_set_workspace(clicked->ws_data->output_id, target_x, target_y);
    return GDK_EVENT_STOP;
}

static void on_workspace_state_changed(const PanelWorkspaceState *state, gpointer user_data) {
    WorkspaceData *data = user_data;
    int workspace_count;
    int current_workspace;

    if (!data || !state) return;
    if (state->grid_width <= 0 || state->grid_height <= 0) return;

    workspace_count = state->grid_width * state->grid_height;
    current_workspace = state->y * state->grid_width + state->x;

    if (data->output_id == state->output_id &&
        data->grid_width == state->grid_width &&
        data->grid_height == state->grid_height &&
        data->workspace_count == workspace_count &&
        data->current_workspace == current_workspace) {
        return;
    }

    data->output_id = state->output_id;
    data->grid_width = state->grid_width;
    data->grid_height = state->grid_height;
    data->workspace_count = workspace_count;
    data->current_workspace = current_workspace;
    rebuild_workspace_buttons(data);
}

static void on_widget_destroy(GtkWidget *widget, gpointer user_data) {
    WorkspaceData *data = user_data;

    (void)widget;

    if (s_workspace_data == data) {
        panel_compositor_backend_set_workspace_callback(NULL, NULL);
        s_workspace_data = NULL;
    }
    g_free(data);
}

GtkWidget *create_workspaces_widget(void) {
    WorkspaceData *data = g_new0(WorkspaceData, 1);
    GtkWidget *event_box = gtk_event_box_new();

    gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box), TRUE);
    gtk_widget_set_app_paintable(event_box, TRUE);
    gtk_widget_set_margin_top(event_box, 4);
    gtk_widget_set_margin_bottom(event_box, 4);
    g_signal_connect(event_box, "draw", G_CALLBACK(on_container_draw), NULL);

    data->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    data->output_id = -1;
    data->grid_width = 3;
    data->grid_height = 3;
    data->workspace_count = 9;
    data->current_workspace = 0;

    gtk_widget_set_margin_start(data->box, 8);
    gtk_widget_set_margin_end(data->box, 8);
    gtk_container_add(GTK_CONTAINER(event_box), data->box);

    s_workspace_data = data;
    rebuild_workspace_buttons(data);
    panel_compositor_backend_set_workspace_callback(on_workspace_state_changed, data);
    g_signal_connect(event_box, "destroy", G_CALLBACK(on_widget_destroy), data);
    return event_box;
}
