#ifndef COMPOSITOR_BACKEND_H
#define COMPOSITOR_BACKEND_H

#include <gtk/gtk.h>

typedef struct {
    int output_id;
    int x;
    int y;
    int grid_width;
    int grid_height;
} PanelWorkspaceState;

typedef struct {
    char layouts[128];
    int layout_index;
} PanelKeyboardState;

typedef void (*PanelWorkspaceStateCallback)(const PanelWorkspaceState *state, gpointer user_data);
typedef void (*PanelKeyboardStateCallback)(const PanelKeyboardState *state, gpointer user_data);

void panel_compositor_backend_init(void);
const char *panel_compositor_backend_name(void);
void panel_compositor_backend_set_workspace_callback(PanelWorkspaceStateCallback cb, gpointer user_data);
void panel_compositor_backend_set_keyboard_callback(PanelKeyboardStateCallback cb, gpointer user_data);
gboolean panel_compositor_backend_set_workspace(int output_id, int x, int y);

#endif
