/*
 * keyboard_layout.c — Keyboard layout indicator for the Aether panel
 */

#include <gtk/gtk.h>
#include <string.h>
#include "keyboard_layout.h"
#include "compositor_backend.h"

typedef struct {
    GtkWidget *box;
    GtkWidget *label;
} KeyboardLayoutWidget;

static GList *s_widgets = NULL;
static char s_current_layout[16] = "US";
static gboolean s_registered = FALSE;

static const char *display_layout_name(const char *name) {
    if (!name || !name[0]) return NULL;
    if (g_ascii_strcasecmp(name, "AR") == 0 ||
        g_ascii_strcasecmp(name, "ARA") == 0 ||
        g_ascii_strcasecmp(name, "IQ") == 0 ||
        g_ascii_strcasecmp(name, "Arabic") == 0)
        return "AR";
    if (g_ascii_strcasecmp(name, "US") == 0 ||
        g_ascii_strcasecmp(name, "USA") == 0 ||
        g_ascii_strcasecmp(name, "EN") == 0 ||
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

static void update_all_widgets(void) {
    for (GList *l = s_widgets; l; l = l->next) {
        KeyboardLayoutWidget *w = l->data;
        if (w && GTK_IS_LABEL(w->label)) {
            gtk_label_set_text(GTK_LABEL(w->label), s_current_layout);
        }
    }
}

static void on_keyboard_state_changed(const PanelKeyboardState *state, gpointer user_data) {
    gchar **layouts;
    const char *display;

    (void)user_data;

    if (!state || !state->layouts[0]) return;

    layouts = g_strsplit(state->layouts, "\n", -1);
    if (!layouts) return;

    if (state->layout_index < 0 || !layouts[state->layout_index]) {
        g_strfreev(layouts);
        return;
    }

    display = display_layout_name(layouts[state->layout_index]);
    g_strlcpy(s_current_layout, display ? display : layouts[state->layout_index], sizeof(s_current_layout));
    normalize_layout(s_current_layout);
    g_strfreev(layouts);
    update_all_widgets();
}

static void on_widget_destroy(GtkWidget *widget, gpointer user_data) {
    KeyboardLayoutWidget *w = user_data;

    (void)widget;

    s_widgets = g_list_remove(s_widgets, w);
    if (!s_widgets && s_registered) {
        panel_compositor_backend_set_keyboard_callback(NULL, NULL);
        s_registered = FALSE;
    }
    g_free(w);
}

GtkWidget *create_keyboard_layout_widget(void) {
    KeyboardLayoutWidget *w = g_new0(KeyboardLayoutWidget, 1);

    if (!s_registered) {
        panel_compositor_backend_set_keyboard_callback(on_keyboard_state_changed, NULL);
        s_registered = TRUE;
    }

    w->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    w->label = gtk_label_new(s_current_layout);

    gtk_widget_set_name(w->box, "keyboard-layout-box");
    gtk_widget_set_name(w->label, "keyboard-layout-label");
    gtk_widget_set_halign(w->box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(w->box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(w->box, 10);

    gtk_container_add(GTK_CONTAINER(w->box), w->label);
    s_widgets = g_list_prepend(s_widgets, w);
    g_signal_connect(w->box, "destroy", G_CALLBACK(on_widget_destroy), w);
    return w->box;
}
