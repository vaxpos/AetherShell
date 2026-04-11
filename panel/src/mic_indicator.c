/*
 * mic_indicator.c
 *
 * Panel microphone indicator:
 *   - A small icon button in the panel bar that shows the current mic level.
 *   - Left-click toggles the Mic Mixer popup that shows per-app sliders.
 *   - Scroll wheel adjusts the master mic volume.
 */

#include "mic_indicator.h"
#include "pulse_volume.h"
#include "window_backend.h"
#include <gtk-layer-shell.h>
#include <string.h>
#include <stdio.h>

static GtkWidget *mi_btn       = NULL;
static GtkWidget *mi_icon      = NULL;
static GtkWidget *mic_mixer_window = NULL;
static GtkWidget *mic_mixer_list   = NULL;
static gboolean   mic_mixer_visible = FALSE;

static const char *mic_icon_name(int pct)
{
    if (pct <= 0)  return "microphone-sensitivity-muted-symbolic";
    if (pct < 33)  return "microphone-sensitivity-low-symbolic";
    if (pct < 66)  return "microphone-sensitivity-medium-symbolic";
    return              "microphone-sensitivity-high-symbolic";
}

typedef struct {
    uint32_t  index;
    GtkWidget *label;
} MicSliderData;

static void on_mic_mixer_app_volume_changed(GtkRange *range, gpointer user_data)
{
    MicSliderData *sd = user_data;
    int val = (int)gtk_range_get_value(range);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", val);
    gtk_label_set_text(GTK_LABEL(sd->label), buf);
    pulse_source_output_set_volume(sd->index, val);
}

static void rebuild_mic_mixer_rows(GList *source_outputs)
{
    if (!mic_mixer_list) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(mic_mixer_list));
    guint n_children = g_list_length(children);
    guint n_inputs = g_list_length(source_outputs);
    
    gboolean is_perfect_match = TRUE;
    
    if (n_inputs == 0) {
        if (n_children == 1 && g_strcmp0(gtk_widget_get_name(GTK_WIDGET(children->data)), "mic-mixer-empty-label") == 0) {
            g_list_free(children);
            return;
        }
        is_perfect_match = FALSE;
    } else if (n_children != n_inputs) {
        is_perfect_match = FALSE;
    } else {
        GList *c = children;
        GList *s = source_outputs;
        while (c && s) {
            GtkWidget *row = GTK_WIDGET(c->data);
            SourceOutputInfo *si = s->data;
            
            GtkWidget *slider = g_object_get_data(G_OBJECT(row), "slider-widget");
            if (!slider) {
                is_perfect_match = FALSE;
                break;
            }
            guint32 row_index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "source-output-index"));
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
        GList *s = source_outputs;
        while (c && s) {
            GtkWidget *row = GTK_WIDGET(c->data);
            SourceOutputInfo *si = s->data;
            
            GtkWidget *slider = g_object_get_data(G_OBJECT(row), "slider-widget");
            MicSliderData *sd = g_object_get_data(G_OBJECT(slider), "slider-data");
            
            if (slider && sd) {
                int cur_val = (int)gtk_range_get_value(GTK_RANGE(slider));
                if (cur_val != si->volume_percent) {
                    g_signal_handlers_block_by_func(slider, on_mic_mixer_app_volume_changed, sd);
                    gtk_range_set_value(GTK_RANGE(slider), si->volume_percent);
                    g_signal_handlers_unblock_by_func(slider, on_mic_mixer_app_volume_changed, sd);
                    
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

    if (!source_outputs) {
        GtkWidget *lbl = gtk_label_new("No applications recording audio");
        gtk_widget_set_name(lbl, "mic-mixer-empty-label");
        gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(mic_mixer_list), lbl, FALSE, FALSE, 0);
        gtk_widget_show_all(mic_mixer_list);
        return;
    }

    for (GList *l = source_outputs; l; l = l->next) {
        SourceOutputInfo *si = l->data;

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_name(row, "mic-mixer-row");

        GtkWidget *icon = gtk_image_new_from_icon_name(
            "audio-input-microphone-symbolic", GTK_ICON_SIZE_MENU);
        gtk_widget_set_name(icon, "mic-mixer-app-icon");

        GtkWidget *name_lbl = gtk_label_new(si->app_name);
        gtk_widget_set_name(name_lbl, "mic-mixer-app-name");
        gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
        gtk_widget_set_size_request(name_lbl, 110, -1);
        gtk_label_set_ellipsize(GTK_LABEL(name_lbl), PANGO_ELLIPSIZE_END);

        GtkWidget *slider = gtk_scale_new_with_range(
            GTK_ORIENTATION_HORIZONTAL, 0, 150, 1);
        gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);
        gtk_range_set_value(GTK_RANGE(slider), si->volume_percent);
        gtk_widget_set_hexpand(slider, TRUE);
        gtk_widget_set_size_request(slider, 120, -1);
        gtk_widget_set_name(slider, "mic-mixer-slider");

        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", si->volume_percent);
        GtkWidget *val_lbl = gtk_label_new(buf);
        gtk_widget_set_name(val_lbl, "mic-mixer-vol-value");
        gtk_widget_set_size_request(val_lbl, 36, -1);
        gtk_widget_set_halign(val_lbl, GTK_ALIGN_END);

        MicSliderData *sd = g_new0(MicSliderData, 1);
        sd->index = si->index;
        sd->label = val_lbl;
        g_object_set_data_full(G_OBJECT(slider), "slider-data", sd, g_free);
        g_signal_connect(slider, "value-changed",
                         G_CALLBACK(on_mic_mixer_app_volume_changed), sd);
                         
        g_object_set_data(G_OBJECT(row), "source-output-index", GUINT_TO_POINTER(si->index));
        g_object_set_data(G_OBJECT(row), "slider-widget", slider);

        gtk_box_pack_start(GTK_BOX(row), icon,     FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), name_lbl, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), slider,   TRUE,  TRUE,  0);
        gtk_box_pack_start(GTK_BOX(row), val_lbl,  FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(mic_mixer_list), row, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(mic_mixer_list);
}

static void on_source_outputs_changed(GList *source_outputs, gpointer user_data)
{
    (void)user_data;
    rebuild_mic_mixer_rows(source_outputs);
    pulse_source_outputs_free(source_outputs);
}

static gboolean on_mic_mixer_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_hide(mic_mixer_window);
        mic_mixer_visible = FALSE;
        return TRUE;
    }
    return FALSE;
}

static void create_mic_mixer_window(void)
{
    if (mic_mixer_window) return;

    mic_mixer_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(mic_mixer_window, "mic-mixer-window");
    gtk_window_set_decorated(GTK_WINDOW(mic_mixer_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(mic_mixer_window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(mic_mixer_window), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(mic_mixer_window), TRUE);

    panel_window_backend_init_popup(GTK_WINDOW(mic_mixer_window),
                                    "vaxpwy-mic-mixer",
                                    GDK_WINDOW_TYPE_HINT_POPUP_MENU,
                                    GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    panel_window_backend_set_anchor(GTK_WINDOW(mic_mixer_window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    panel_window_backend_set_anchor(GTK_WINDOW(mic_mixer_window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    panel_window_backend_set_margin(GTK_WINDOW(mic_mixer_window), GTK_LAYER_SHELL_EDGE_TOP, 32);
    panel_window_backend_set_margin(GTK_WINDOW(mic_mixer_window), GTK_LAYER_SHELL_EDGE_RIGHT, 0);

    GdkScreen  *scr = gtk_widget_get_screen(mic_mixer_window);
    GdkVisual  *vis = gdk_screen_get_rgba_visual(scr);
    if (vis && gdk_screen_is_composited(scr)) {
        gtk_widget_set_visual(mic_mixer_window, vis);
        gtk_widget_set_app_paintable(mic_mixer_window, TRUE);
    }

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(outer, "mic-mixer-outer");
    gtk_container_add(GTK_CONTAINER(mic_mixer_window), outer);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(header, "mic-mixer-header");

    GtkWidget *hdr_icon = gtk_image_new_from_icon_name(
        "microphone-sensitivity-high-symbolic", GTK_ICON_SIZE_MENU);
    GtkWidget *hdr_lbl  = gtk_label_new("Microphone Mixer");
    gtk_widget_set_name(hdr_lbl, "mic-mixer-title");

    gtk_box_pack_start(GTK_BOX(header), hdr_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), hdr_lbl,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer),  header,   FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(sep, "mic-mixer-sep");
    gtk_box_pack_start(GTK_BOX(outer), sep, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, 320, -1);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 300);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);

    mic_mixer_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_name(mic_mixer_list, "mic-mixer-list");
    gtk_widget_set_margin_top   (mic_mixer_list, 8);
    gtk_widget_set_margin_bottom(mic_mixer_list, 8);
    gtk_widget_set_margin_start (mic_mixer_list, 12);
    gtk_widget_set_margin_end   (mic_mixer_list, 12);

    gtk_container_add(GTK_CONTAINER(scroll), mic_mixer_list);
    gtk_box_pack_start(GTK_BOX(outer), scroll, TRUE, TRUE, 0);

    g_signal_connect(mic_mixer_window, "key-press-event",
                     G_CALLBACK(on_mic_mixer_key_press), NULL);

    gtk_widget_show_all(outer);
    gtk_widget_hide(mic_mixer_window);

    pulse_source_outputs_init(on_source_outputs_changed, NULL);
}

static void on_mic_volume_changed(int percent, gboolean muted, gpointer user_data)
{
    (void)user_data;
    if (!mi_icon) return;
    
    const char *icon_name;
    if (muted) {
        icon_name = "microphone-sensitivity-muted-symbolic";
    } else {
        icon_name = mic_icon_name(percent);
    }
    
    gtk_image_set_from_icon_name(GTK_IMAGE(mi_icon),
                                 icon_name,
                                 GTK_ICON_SIZE_MENU);
}

static gboolean on_mi_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
    (void)widget; (void)user_data;
    int cur = pulse_mic_get_current();
    if (cur < 0) cur = 50;

    if (event->direction == GDK_SCROLL_UP)
        cur = MIN(150, cur + 5);
    else if (event->direction == GDK_SCROLL_DOWN)
        cur = MAX(0, cur - 5);
    else
        return FALSE;

    pulse_mic_set(cur);
    return TRUE;
}

static void on_mi_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    if (!mic_mixer_window) return;

    if (mic_mixer_visible) {
        gtk_widget_hide(mic_mixer_window);
        mic_mixer_visible = FALSE;
    } else {
        gtk_widget_show_all(mic_mixer_window);
        mic_mixer_visible = TRUE;
    }
}

GtkWidget *create_mic_indicator_widget(void)
{
    mi_btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(mi_btn), GTK_RELIEF_NONE);
    gtk_widget_set_name(mi_btn, "mic-indicator-btn");

    mi_icon = gtk_image_new_from_icon_name(
        "microphone-sensitivity-medium-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(mi_btn), mi_icon);

    gtk_widget_add_events(mi_btn, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(mi_btn, "clicked",      G_CALLBACK(on_mi_clicked), NULL);
    g_signal_connect(mi_btn, "scroll-event", G_CALLBACK(on_mi_scroll),  NULL);

    pulse_mic_init(on_mic_volume_changed, NULL);

    create_mic_mixer_window();

    return mi_btn;
}
