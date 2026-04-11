#ifndef WORKSPACES_XORG_H
#define WORKSPACES_XORG_H

#include <gtk/gtk.h>
#include <X11/Xlib.h>

typedef struct {
    Display *dpy;
    Window root;
    int current_desktop;
    int num_desktops;
} XorgWorkspaceState;

gboolean xorg_workspaces_init(XorgWorkspaceState *state);
gboolean xorg_workspaces_refresh(XorgWorkspaceState *state);
gboolean xorg_workspaces_switch(const XorgWorkspaceState *state, int desktop_idx);

#endif
