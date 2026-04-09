/*
 * volume_indicator.c
 *
 * Panel volume indicator:
 *   - A small icon button in the panel bar that shows the current volume level
 *     dynamically (muted / low / medium / high).
 *   - Left-click toggles the Volume Mixer popup that shows per-app sliders.
 *   - Scroll wheel adjusts the master volume.
 */

#include "volume_indicator.h"
#include "pulse_volume.h"
#include <gtk-layer-shell.h>
#include <string.h>
#include <stdio.h>

/* ── State ───────────────────────────────────────────────────────────────── */

static GtkWidget *vi_btn       = NULL;   /* panel button               */
static GtkWidget *vi_icon      = NULL;   /* icon inside the button     */
static GtkWidget *mixer_window = NULL;   /* floating mixer popup       */
static GtkWidget *mixer_list   = NULL;   /* VBox – one row per app     */
static gboolean   mixer_visible = FALSE;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static const char *volume_icon_name(int pct)
{
    if (pct <= 0)  return "audio-volume-muted-symbolic";
    if (pct < 33)  return "audio-volume-low-symbolic";
    if (pct < 66)  return "audio-volume-medium-symbolic";
    return              "audio-volume-high-symbolic";
}

/* ── Mixer popup ─────────────────────────────────────────────────────────── */

/* Data attached to each slider so the value-changed callback knows which
 * PulseAudio sink-input to control. */
typedef struct {
    uint32_t  index;
    GtkWidget *label;
} SliderData;

static void on_mixer_app_volume_changed(GtkRange *range, gpointer user_data)
{
    SliderData *sd = user_data;
    int val = (int)gtk_range_get_value(range);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", val);
    gtk_label_set_text(GTK_LABEL(sd->label), buf);
    pulse_sink_input_set_volume(sd->index, val);
}

static void rebuild_mixer_rows(GList *sink_inputs)
{
    if (!mixer_list) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(mixer_list));
    guint n_children = g_list_length(children);
    guint n_inputs = g_list_length(sink_inputs);
    
    gboolean is_perfect_match = TRUE;
    
    if (n_inputs == 0) {
        if (n_children == 1 && g_strcmp0(gtk_widget_get_name(GTK_WIDGET(children->data)), "mixer-empty-label") == 0) {
            g_list_free(children);
            return;
        }
        is_perfect_match = FALSE;
    } else if (n_children != n_inputs) {
        is_perfect_match = FALSE;
    } else {
        GList *c = children;
        GList *s = sink_inputs;
        while (c && s) {
            GtkWidget *row = GTK_WIDGET(c->data);
            SinkInputInfo *si = s->data;
            
            GtkWidget *slider = g_object_get_data(G_OBJECT(row), "slider-widget");
            if (!slider) {
                is_perfect_match = FALSE;
                break;
            }
            guint32 row_index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "sink-index"));
            if (row_index != si->index) {
                is_perfect_match = FALSE;
                break;
            }
            c = c->next;
            s = s->next;
        }
    }
    
    if (is_perfect_match) {
        GList *c = children;
        GList *s = sink_inputs;
        while (c && s) {
            GtkWidget *row = GTK_WIDGET(c->data);
            SinkInputInfo *si = s->data;
            
            GtkWidget *slider = g_object_get_data(G_OBJECT(row), "slider-widget");
            SliderData *sd = g_object_get_data(G_OBJECT(slider), "slider-data");
            
            if (slider && sd) {
                int cur_val = (int)gtk_range_get_value(GTK_RANGE(slider));
                if (cur_val != si->volume_percent) {
                    g_signal_handlers_block_by_func(slider, on_mixer_app_volume_changed, sd);
                    gtk_range_set_value(GTK_RANGE(slider), si->volume_percent);
                    g_signal_handlers_unblock_by_func(slider, on_mixer_app_volume_changed, sd);
                    
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%d%%", si->volume_percent);
                    gtk_label_set_text(GTK_LABEL(sd->label), buf);
                }
            }
            c = c->next;
            s = s->next;
        }
        g_list_free(children);
        return;
    }

    /* Remove all existing children */
    for (GList *c = children; c; c = c->next)
        gtk_widget_destroy(GTK_WIDGET(c->data));
    g_list_free(children);

    if (!sink_inputs) {
        /* Empty state */
        GtkWidget *lbl = gtk_label_new("No applications playing audio");
        gtk_widget_set_name(lbl, "mixer-empty-label");
        gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(mixer_list), lbl, FALSE, FALSE, 0);
        gtk_widget_show_all(mixer_list);
        return;
    }

    for (GList *l = sink_inputs; l; l = l->next) {
        SinkInputInfo *si = l->data;

        /* Row: icon + app_name on left, slider + value label on right */
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_name(row, "mixer-row");

        /* App icon (generic) */
        GtkWidget *icon = gtk_image_new_from_icon_name(
            "audio-x-generic-symbolic", GTK_ICON_SIZE_MENU);
        gtk_widget_set_name(icon, "mixer-app-icon");

        /* App name */
        GtkWidget *name_lbl = gtk_label_new(si->app_name);
        gtk_widget_set_name(name_lbl, "mixer-app-name");
        gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
        gtk_widget_set_size_request(name_lbl, 110, -1);
        gtk_label_set_ellipsize(GTK_LABEL(name_lbl), PANGO_ELLIPSIZE_END);

        /* Slider */
        GtkWidget *slider = gtk_scale_new_with_range(
            GTK_ORIENTATION_HORIZONTAL, 0, 150, 1);
        gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);
        gtk_range_set_value(GTK_RANGE(slider), si->volume_percent);
        gtk_widget_set_hexpand(slider, TRUE);
        gtk_widget_set_size_request(slider, 120, -1);
        gtk_widget_set_name(slider, "mixer-slider");

        /* Value label */
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", si->volume_percent);
        GtkWidget *val_lbl = gtk_label_new(buf);
        gtk_widget_set_name(val_lbl, "mixer-vol-value");
        gtk_widget_set_size_request(val_lbl, 36, -1);
        gtk_widget_set_halign(val_lbl, GTK_ALIGN_END);

        SliderData *sd = g_new0(SliderData, 1);
        sd->index = si->index;
        sd->label = val_lbl;
        g_object_set_data_full(G_OBJECT(slider), "slider-data", sd, g_free);
        g_signal_connect(slider, "value-changed",
                         G_CALLBACK(on_mixer_app_volume_changed), sd);
                         
        g_object_set_data(G_OBJECT(row), "sink-index", GUINT_TO_POINTER(si->index));
        g_object_set_data(G_OBJECT(row), "slider-widget", slider);

        gtk_box_pack_start(GTK_BOX(row), icon,     FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), name_lbl, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), slider,   TRUE,  TRUE,  0);
        gtk_box_pack_start(GTK_BOX(row), val_lbl,  FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(mixer_list), row, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(mixer_list);
}

static void on_sink_inputs_changed(GList *sink_inputs, gpointer user_data)
{
    (void)user_data;
    rebuild_mixer_rows(sink_inputs);
    pulse_sink_inputs_free(sink_inputs);
}

static gboolean on_mixer_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_hide(mixer_window);
        mixer_visible = FALSE;
        return TRUE;
    }
    return FALSE;
}

static void create_mixer_window(void)
{
    if (mixer_window) return;

    mixer_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(mixer_window, "volume-mixer-window");
    gtk_window_set_decorated(GTK_WINDOW(mixer_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(mixer_window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(mixer_window), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(mixer_window), TRUE);

    /* Use gtk-layer-shell so it floats above everything on Wayland */
    gtk_layer_init_for_window(GTK_WINDOW(mixer_window));
    gtk_layer_set_namespace(GTK_WINDOW(mixer_window), "vaxpwy-volume-mixer");
    gtk_layer_set_layer(GTK_WINDOW(mixer_window), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(mixer_window), GTK_LAYER_SHELL_EDGE_TOP,   TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(mixer_window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(mixer_window), GTK_LAYER_SHELL_EDGE_TOP,   32);
    gtk_layer_set_margin(GTK_WINDOW(mixer_window), GTK_LAYER_SHELL_EDGE_RIGHT,  0);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(mixer_window), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    /* RGBA visual for transparency */
    GdkScreen  *scr = gtk_widget_get_screen(mixer_window);
    GdkVisual  *vis = gdk_screen_get_rgba_visual(scr);
    if (vis && gdk_screen_is_composited(scr)) {
        gtk_widget_set_visual(mixer_window, vis);
        gtk_widget_set_app_paintable(mixer_window, TRUE);
    }

    /* Outer container */
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(outer, "mixer-outer");
    gtk_container_add(GTK_CONTAINER(mixer_window), outer);

    /* Header */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(header, "mixer-header");

    GtkWidget *hdr_icon = gtk_image_new_from_icon_name(
        "audio-volume-high-symbolic", GTK_ICON_SIZE_MENU);
    GtkWidget *hdr_lbl  = gtk_label_new("Volume Mixer");
    gtk_widget_set_name(hdr_lbl, "mixer-title");

    gtk_box_pack_start(GTK_BOX(header), hdr_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), hdr_lbl,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer),  header,   FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(sep, "mixer-sep");
    gtk_box_pack_start(GTK_BOX(outer), sep, FALSE, FALSE, 0);

    /* Scrollable list of apps */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, 320, -1);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 300);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);

    mixer_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_name(mixer_list, "mixer-list");
    gtk_widget_set_margin_top   (mixer_list, 8);
    gtk_widget_set_margin_bottom(mixer_list, 8);
    gtk_widget_set_margin_start (mixer_list, 12);
    gtk_widget_set_margin_end   (mixer_list, 12);

    gtk_container_add(GTK_CONTAINER(scroll), mixer_list);
    gtk_box_pack_start(GTK_BOX(outer), scroll, TRUE, TRUE, 0);

    g_signal_connect(mixer_window, "key-press-event",
                     G_CALLBACK(on_mixer_key_press), NULL);

    gtk_widget_show_all(outer);
    gtk_widget_hide(mixer_window);

    /* Register for sink-input updates */
    pulse_sink_inputs_init(on_sink_inputs_changed, NULL);
}

/* ── Panel indicator ─────────────────────────────────────────────────────── */

static void on_volume_changed(int percent, gboolean muted, gpointer user_data)
{
    (void)user_data;
    if (!vi_icon) return;
    
    const char *icon_name;
    if (muted) {
        icon_name = "audio-volume-muted-symbolic";
    } else {
        icon_name = volume_icon_name(percent);
    }
    
    gtk_image_set_from_icon_name(GTK_IMAGE(vi_icon),
                                 icon_name,
                                 GTK_ICON_SIZE_MENU);
}

static gboolean on_vi_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
    (void)widget; (void)user_data;
    int cur = pulse_volume_get_current();
    if (cur < 0) cur = 50;

    if (event->direction == GDK_SCROLL_UP)
        cur = MIN(150, cur + 5);
    else if (event->direction == GDK_SCROLL_DOWN)
        cur = MAX(0, cur - 5);
    else
        return FALSE;

    pulse_volume_set(cur);
    return TRUE;
}

static void on_vi_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    if (!mixer_window) return;

    if (mixer_visible) {
        gtk_widget_hide(mixer_window);
        mixer_visible = FALSE;
    } else {
        gtk_widget_show_all(mixer_window);
        mixer_visible = TRUE;
    }
}

GtkWidget *create_volume_indicator_widget(void)
{
    /* Initialise pulse — must be called after pulse_volume_init in control_center */
    /* (pulse_volume_init is already called from init_control_center, so context
     *  exists by the time the panel is shown.)                                  */

    vi_btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(vi_btn), GTK_RELIEF_NONE);
    gtk_widget_set_name(vi_btn, "volume-indicator-btn");

    vi_icon = gtk_image_new_from_icon_name(
        "audio-volume-medium-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(vi_btn), vi_icon);

    gtk_widget_add_events(vi_btn, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(vi_btn, "clicked",      G_CALLBACK(on_vi_clicked), NULL);
    g_signal_connect(vi_btn, "scroll-event", G_CALLBACK(on_vi_scroll),  NULL);

    /* Register for master volume updates */
    pulse_volume_init(on_volume_changed, NULL);

    /* Build the mixer window (hidden initially) */
    create_mixer_window();

    return vi_btn;
}
