#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include <math.h>
#include "pulse_volume.h"
#include "power_profile.h"
#include "mpris_control.h"
#include "power_actions.h"
#include "venom_notifications.h"
#include "resource_paths.h"
#include "control_center_ui.h"
#include "network_actions.h"
#include "brightness_control.h"

static GtkWidget *bt_wifi = NULL;
static GtkWidget *bt_eth = NULL;

static void on_wifi_state_changed(gboolean enabled) {
    if (!bt_wifi) return;
    GtkStyleContext *ctx = gtk_widget_get_style_context(bt_wifi);
    if (enabled) {
        gtk_style_context_add_class(ctx, "active-cyan");
    } else {
        gtk_style_context_remove_class(ctx, "active-cyan");
    }
}

static void on_eth_state_changed(gboolean enabled) {
    if (!bt_eth) return;
    GtkStyleContext *ctx = gtk_widget_get_style_context(bt_eth);
    if (enabled) {
        gtk_style_context_add_class(ctx, "active-cyan");
    } else {
        gtk_style_context_remove_class(ctx, "active-cyan");
    }
}

static void on_wifi_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    network_toggle_wifi();
}

static GtkWidget *wifi_popover = NULL;
static GtkWidget *wifi_listbox = NULL;

typedef struct {
    char *ssid;
    char *bssid;
    gboolean secured;
} ConnectAttempt;

static void wifi_connect_callback_with_prompt(gboolean success, gpointer user_data);

static void ask_for_wifi_password_and_connect(const char *ssid, const char *bssid, gboolean show_error) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Wi-Fi Password", NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Connect", GTK_RESPONSE_ACCEPT, "Cancel", GTK_RESPONSE_REJECT, NULL);
        
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    if (show_error) {
        GtkWidget *err_lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(err_lbl), "<span foreground='red'>Wrong password, please try again.</span>");
        gtk_widget_set_margin_top(err_lbl, 6);
        gtk_widget_set_margin_bottom(err_lbl, 6);
        gtk_box_pack_start(GTK_BOX(content), err_lbl, FALSE, FALSE, 0);
    }

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Password");
    gtk_box_pack_start(GTK_BOX(content), entry, TRUE, TRUE, 0);
    gtk_widget_set_margin_start(entry, 12);
    gtk_widget_set_margin_end(entry, 12);
    gtk_widget_set_margin_bottom(entry, 12);
    gtk_widget_set_margin_top(entry, show_error ? 6 : 12);
    gtk_widget_show_all(content);
    
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        const gchar *pass = gtk_entry_get_text(GTK_ENTRY(entry));
        ConnectAttempt *attempt = g_new0(ConnectAttempt, 1);
        attempt->ssid = g_strdup(ssid);
        attempt->bssid = g_strdup(bssid);
        attempt->secured = TRUE;
        network_wifi_connect(ssid, bssid, pass, wifi_connect_callback_with_prompt, attempt);
    }
    gtk_widget_destroy(dialog);
}

static void wifi_connect_callback_with_prompt(gboolean success, gpointer user_data) {
    ConnectAttempt *attempt = user_data;
    if (!success) {
        if (attempt->secured) {
            ask_for_wifi_password_and_connect(attempt->ssid, attempt->bssid, TRUE);
        }
    }
    g_free(attempt->ssid);
    g_free(attempt->bssid);
    g_free(attempt);
}

static void on_wifi_network_connect_item(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box; (void)user_data;
    if (!row) return;
    
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(row));
    if (!child) return;
    
    const char *ssid = g_object_get_data(G_OBJECT(child), "ssid");
    const char *bssid = g_object_get_data(G_OBJECT(child), "bssid");
    gboolean secured = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "secured"));
    gboolean saved = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "saved"));
    
    if (!ssid) return;
    
    if (wifi_popover) {
        gtk_popover_popdown(GTK_POPOVER(wifi_popover));
    }
    
    if (secured && !saved) {
        ask_for_wifi_password_and_connect(ssid, bssid, FALSE);
    } else {
        ConnectAttempt *attempt = g_new0(ConnectAttempt, 1);
        attempt->ssid = g_strdup(ssid);
        attempt->bssid = g_strdup(bssid);
        attempt->secured = secured;
        network_wifi_connect(ssid, bssid, NULL, wifi_connect_callback_with_prompt, attempt);
    }
}

static void on_wifi_scan_done(GList *networks, gpointer user_data) {
    (void)user_data;
    if (!wifi_listbox) {
        network_wifi_networks_free(networks);
        return;
    }
    
    GList *children = gtk_container_get_children(GTK_CONTAINER(wifi_listbox));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
    
    if (!networks) {
        GtkWidget *lbl = gtk_label_new("No networks found");
        gtk_widget_set_margin_top(lbl, 16);
        gtk_widget_set_margin_bottom(lbl, 16);
        gtk_container_add(GTK_CONTAINER(wifi_listbox), lbl);
    } else {
        for (GList *l = networks; l != NULL; l = l->next) {
            WifiNetwork *net = l->data;
            
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_set_margin_start(box, 8);
            gtk_widget_set_margin_end(box, 8);
            gtk_widget_set_margin_top(box, 6);
            gtk_widget_set_margin_bottom(box, 6);
            
            const char *icon_name = "network-wireless-signal-excellent-symbolic";
            if (net->strength < 25) icon_name = "network-wireless-signal-weak-symbolic";
            else if (net->strength < 50) icon_name = "network-wireless-signal-ok-symbolic";
            else if (net->strength < 75) icon_name = "network-wireless-signal-good-symbolic";
            
            GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
            GtkWidget *label = gtk_label_new(net->ssid);
            gtk_widget_set_halign(label, GTK_ALIGN_START);
            
            gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
            
            if (net->connected) {
                GtkWidget *connected_icon = gtk_image_new_from_icon_name("emblem-ok-symbolic", GTK_ICON_SIZE_MENU);
                gtk_box_pack_end(GTK_BOX(box), connected_icon, FALSE, FALSE, 0);
            } else if (net->secured) {
                GtkWidget *lock = gtk_image_new_from_icon_name("changes-prevent-symbolic", GTK_ICON_SIZE_MENU);
                gtk_box_pack_end(GTK_BOX(box), lock, FALSE, FALSE, 0);
            }
            
            g_object_set_data_full(G_OBJECT(box), "ssid", g_strdup(net->ssid), g_free);
            g_object_set_data_full(G_OBJECT(box), "bssid", g_strdup(net->bssid), g_free);
            g_object_set_data(G_OBJECT(box), "secured", GINT_TO_POINTER(net->secured));
            g_object_set_data(G_OBJECT(box), "saved", GINT_TO_POINTER(net->saved));
            
            gtk_container_add(GTK_CONTAINER(wifi_listbox), box);
        }
    }
    gtk_widget_show_all(wifi_listbox);
    network_wifi_networks_free(networks);
}

static void on_wifi_popover_closed(GtkPopover *popover, gpointer user_data) {
    (void)popover; (void)user_data;
    // We keep the widget to be reused. Just hide it.
}

static gboolean on_wifi_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)user_data;
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        if (!wifi_popover) {
            wifi_popover = gtk_popover_new(widget);
            gtk_popover_set_position(GTK_POPOVER(wifi_popover), GTK_POS_BOTTOM);
            
            GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
            gtk_widget_set_size_request(scroll, 240, 260); // Set max height essentially by forcing request sizes
            
            wifi_listbox = gtk_list_box_new();
            g_signal_connect(wifi_listbox, "row-activated", G_CALLBACK(on_wifi_network_connect_item), NULL);
            gtk_container_add(GTK_CONTAINER(scroll), wifi_listbox);
            
            gtk_container_add(GTK_CONTAINER(wifi_popover), scroll);
            gtk_style_context_add_class(gtk_widget_get_style_context(wifi_popover), "wifi-menu");
            g_signal_connect(wifi_popover, "closed", G_CALLBACK(on_wifi_popover_closed), NULL);
        }
        
        // Clear list and add "Scanning"
        GList *children = gtk_container_get_children(GTK_CONTAINER(wifi_listbox));
        for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        }
        g_list_free(children);
        
        GtkWidget *lbl = gtk_label_new("Scanning...");
        gtk_widget_set_margin_top(lbl, 16);
        gtk_widget_set_margin_bottom(lbl, 16);
        gtk_container_add(GTK_CONTAINER(wifi_listbox), lbl);
        
        gtk_widget_show_all(wifi_popover);
        gtk_popover_popup(GTK_POPOVER(wifi_popover));
        
        network_wifi_scan(on_wifi_scan_done, NULL);
        return TRUE;
    }
    return FALSE;
}

static void on_eth_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    network_toggle_ethernet();
}

static gboolean draw_control_center_background(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    GtkAllocation alloc;
    const double radius = 12.0;

    (void)user_data;
    gtk_widget_get_allocation(widget, &alloc);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.392);
    cairo_new_sub_path(cr);
    cairo_move_to(cr, 0, radius);
    cairo_arc(cr, radius, radius, radius, G_PI, 3.0 * G_PI / 2.0);
    cairo_line_to(cr, alloc.width, 0);
    cairo_arc(cr, alloc.width - radius, alloc.height - radius, radius, 0, G_PI / 2.0);
    cairo_arc(cr, radius, alloc.height - radius, radius, G_PI / 2.0, G_PI);
    cairo_close_path(cr);
    cairo_fill_preserve(cr);

    cairo_set_source_rgba(cr, 0.133, 0.133, 0.133, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    return FALSE;
}

static void ensure_rgba_visual(GtkWidget *widget) {
    GdkScreen *screen;
    GdkVisual *visual;

    if (!widget)
        return;

    screen = gtk_widget_get_screen(widget);
    if (!screen)
        return;

    visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(widget, visual);
        gtk_widget_set_app_paintable(widget, TRUE);
    }
}

void control_center_set_relative_to(GtkWidget *control_center, GtkWidget *relative_to) {
    (void)control_center;
    (void)relative_to;
}

GtkWidget *stack;
GtkWidget *btn_controls;
GtkWidget *btn_notifications;

GtkWidget *scale_snd;
GtkWidget *lbl_snd_val;
static gboolean updating_from_pulse = FALSE;

GtkWidget *scale_disp_ref;          /* brightness slider — kept for callback */
GtkWidget *lbl_disp_val_ref;        /* brightness label  */
static gboolean updating_from_brightness = FALSE;

static void on_disp_scale_changed(GtkRange *range, gpointer user_data) {
    if (updating_from_brightness) return;
    int val = (int)gtk_range_get_value(range);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", val);
    gtk_label_set_text(GTK_LABEL(lbl_disp_val_ref), buf);
    brightness_set(val);
}

static void on_brightness_changed(int percent, gpointer user_data) {
    if (!scale_disp_ref) return;
    updating_from_brightness = TRUE;
    gtk_range_set_value(GTK_RANGE(scale_disp_ref), percent);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    gtk_label_set_text(GTK_LABEL(lbl_disp_val_ref), buf);
    updating_from_brightness = FALSE;
}

static void on_snd_scale_changed(GtkRange *range, gpointer user_data) {
    if (updating_from_pulse) return;
    double val = gtk_range_get_value(range);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", (int)val);
    gtk_label_set_text(GTK_LABEL(lbl_snd_val), buf);
    pulse_volume_set((int)val);
}

static GtkWidget *snd_out_menu = NULL;
static GtkWidget *btn_snd_out = NULL;

static void on_snd_out_item_activated(GtkMenuItem *item, gpointer user_data) {
    (void)user_data;
    const char *sink_name = g_object_get_data(G_OBJECT(item), "sink_name");
    const char *port_name = g_object_get_data(G_OBJECT(item), "port_name");

    if (sink_name) {
        pulse_device_set(sink_name, port_name);
    }
}

static void on_sinks_fetched(GList *sinks, gpointer user_data) {
    (void)user_data;
    if (!snd_out_menu) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(snd_out_menu));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    for (GList *l = sinks; l != NULL; l = l->next) {
        AudioSinkInfo *si = l->data;
        GtkWidget *item = gtk_menu_item_new();
        
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(box, 8);
        gtk_widget_set_margin_end(box, 8);
        gtk_widget_set_margin_top(box, 6);
        gtk_widget_set_margin_bottom(box, 6);

        GtkWidget *icon = gtk_image_new_from_icon_name("audio-speakers-symbolic", GTK_ICON_SIZE_MENU);
        GtkWidget *label = gtk_label_new(si->description ? si->description : (si->port_name ? si->port_name : si->sink_name));
        gtk_widget_set_halign(label, GTK_ALIGN_START);

        gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

        if (si->is_active) {
            GtkWidget *connected_icon = gtk_image_new_from_icon_name("emblem-ok-symbolic", GTK_ICON_SIZE_MENU);
            gtk_box_pack_end(GTK_BOX(box), connected_icon, FALSE, FALSE, 0);
        }

        gtk_container_add(GTK_CONTAINER(item), box);
        g_object_set_data_full(G_OBJECT(item), "sink_name", g_strdup(si->sink_name), g_free);
        if (si->port_name) {
            g_object_set_data_full(G_OBJECT(item), "port_name", g_strdup(si->port_name), g_free);
        }

        g_signal_connect(item, "activate", G_CALLBACK(on_snd_out_item_activated), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(snd_out_menu), item);
    }
    
    if (!sinks) {
        GtkWidget *item = gtk_menu_item_new_with_label("No devices found");
        gtk_widget_set_sensitive(item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(snd_out_menu), item);
    }

    gtk_widget_show_all(snd_out_menu);
    gtk_menu_popup_at_widget(GTK_MENU(snd_out_menu), btn_snd_out, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);

    pulse_sinks_free(sinks);
}

static void on_snd_out_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    if (!snd_out_menu) {
        snd_out_menu = gtk_menu_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(snd_out_menu), "wifi-menu");
    }
    pulse_sinks_get(on_sinks_fetched, NULL);
}

static void on_pulse_volume_changed(int percent, gboolean muted, gpointer user_data) {
    (void)user_data;
    updating_from_pulse = TRUE;
    gtk_range_set_value(GTK_RANGE(scale_snd), percent);
    char buf[32];
    if (muted) {
        snprintf(buf, sizeof(buf), "Muted");
    } else {
        snprintf(buf, sizeof(buf), "%d%%", percent);
    }
    gtk_label_set_text(GTK_LABEL(lbl_snd_val), buf);
    updating_from_pulse = FALSE;
}

GtkWidget *perf_icons_widgets[3];
GtkWidget *perf_labels_widgets[3];
const char *profile_names[] = {"power-saver", "balanced", "performance"};

static void update_perf_ui(const char *profile) {
    for (int i = 0; i < 3; i++) {
        GtkStyleContext *ctx_icon = gtk_widget_get_style_context(perf_icons_widgets[i]);
        GtkStyleContext *ctx_label = gtk_widget_get_style_context(perf_labels_widgets[i]);
        if (g_strcmp0(profile_names[i], profile) == 0) {
            gtk_style_context_add_class(ctx_icon, "active-cyan-text");
            gtk_style_context_add_class(ctx_label, "active-cyan-text");
        } else {
            gtk_style_context_remove_class(ctx_icon, "active-cyan-text");
            gtk_style_context_remove_class(ctx_label, "active-cyan-text");
        }
    }
}

static void on_power_profile_changed(const char *profile, gpointer user_data) {
    update_perf_ui(profile);
}

static void on_perf_btn_clicked(GtkButton *btn, gpointer user_data) {
    const char *profile = (const char*)g_object_get_data(G_OBJECT(btn), "profile");
    if (profile) {
        power_profile_set(profile);
    }
}

// Media Control integration
GtkWidget *m_btn1; // Prev
GtkWidget *m_btn2; // Play/Pause
GtkWidget *m_btn3; // Next
GtkWidget *m_art_img;
GtkWidget *m_title_lbl;
GtkWidget *m_artist_lbl;

static GdkPixbuf *create_rounded_pixbuf(GdkPixbuf *src, int radius) {
    if (!src) return NULL;
    int w = gdk_pixbuf_get_width(src);
    int h = gdk_pixbuf_get_height(src);
    
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(surface);
    
    // Smooth transparent background
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    
    double degrees = G_PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, w - radius, radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc(cr, w - radius, h - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc(cr, radius, h - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc(cr, radius, radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path(cr);
    
    cairo_clip(cr);
    
    gdk_cairo_set_source_pixbuf(cr, src, 0, 0);
    cairo_paint(cr);
    
    GdkPixbuf *dest = gdk_pixbuf_get_from_surface(surface, 0, 0, w, h);
    
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    
    return dest;
}

static void on_mpris_state_changed(gboolean is_playing, const char *title, const char *artist, const char *art_url, gpointer user_data) {
    // Update play/pause icon based on is_playing
    GtkWidget *new_icon;
    if (is_playing) {
        new_icon = gtk_image_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    } else {
        new_icon = gtk_image_new_from_icon_name("media-playback-start-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    }
    gtk_container_remove(GTK_CONTAINER(m_btn2), gtk_bin_get_child(GTK_BIN(m_btn2)));
    gtk_container_add(GTK_CONTAINER(m_btn2), new_icon);
    gtk_widget_show_all(m_btn2);

    // Update Text
    gtk_label_set_text(GTK_LABEL(m_title_lbl), title ? title : "Not Playing");
    gtk_label_set_text(GTK_LABEL(m_artist_lbl), artist ? artist : "Unknown Artist");

    // Update Art
    if (art_url && strncmp(art_url, "file://", 7) == 0) {
        gchar *path = g_uri_unescape_string(art_url + 7, NULL);
        if (path && g_file_test(path, G_FILE_TEST_EXISTS)) {
            GdkPixbuf *full_pb = gdk_pixbuf_new_from_file(path, NULL);
            if (full_pb) {
                GdkPixbuf *scaled = gdk_pixbuf_scale_simple(full_pb, 64, 64, GDK_INTERP_BILINEAR);
                GdkPixbuf *rounded = create_rounded_pixbuf(scaled, 12);
                gtk_image_set_from_pixbuf(GTK_IMAGE(m_art_img), rounded ? rounded : scaled);
                if (rounded) g_object_unref(rounded);
                g_object_unref(scaled);
                g_object_unref(full_pb);
            } else {
                gtk_image_set_from_icon_name(GTK_IMAGE(m_art_img), "audio-x-generic-symbolic", GTK_ICON_SIZE_DIALOG);
            }
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(m_art_img), "audio-x-generic-symbolic", GTK_ICON_SIZE_DIALOG);
        }
        g_free(path);
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(m_art_img), "audio-x-generic-symbolic", GTK_ICON_SIZE_DIALOG);
        // Note: HTTP urls could be fetched here asynchronously if needed
    }
}

static void on_media_prev_clicked(GtkButton *btn, gpointer user_data) {
    mpris_control_prev();
}
static void on_media_play_clicked(GtkButton *btn, gpointer user_data) {
    mpris_control_play_pause();
}
static void on_media_next_clicked(GtkButton *btn, gpointer user_data) {
    mpris_control_next();
}



// Notifications Integration
GtkWidget *sw_dnd;
GtkWidget *notifications_box;

static void on_dnd_state_changed(gboolean enabled, gpointer user_data) {
    // Only update switch if it differs to avoid loops
    if (gtk_switch_get_active(GTK_SWITCH(sw_dnd)) != enabled) {
        gtk_switch_set_active(GTK_SWITCH(sw_dnd), enabled);
    }
}

static gboolean on_dnd_switch_activated(GtkWidget *widget, gboolean state, gpointer user_data) {
    venom_notifications_set_dnd(state);
    return FALSE;
}

static void on_clear_history_clicked(GtkButton *btn, gpointer user_data) {
    venom_notifications_clear_history();
}

static GtkWidget* create_icon_button(const char *icon_name, const char *extra_class) {
    GtkWidget *btn = gtk_button_new();
    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_container_add(GTK_CONTAINER(btn), icon);
    GtkStyleContext *ctx = gtk_widget_get_style_context(btn);
    gtk_style_context_add_class(ctx, "circular-icon-btn");
    if (extra_class) {
        gtk_style_context_add_class(ctx, extra_class);
    }
    return btn;
}

static void on_notifications_updated(GList *history, gpointer user_data) {
    // Clear current children of notifications_box
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(notifications_box));
    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    if (!history) {
        // Show empty state
        GtkWidget *icon = gtk_image_new_from_icon_name("notifications-disabled-symbolic", GTK_ICON_SIZE_DIALOG);
        gtk_style_context_add_class(gtk_widget_get_style_context(icon), "notification-icon");
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 72);

        GtkWidget *lbl = gtk_label_new("No Notifications");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "notification-label");

        // Center empty state
        GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);
        gtk_widget_set_vexpand(empty_box, TRUE);
        gtk_widget_set_hexpand(empty_box, TRUE);
        
        gtk_box_pack_start(GTK_BOX(empty_box), icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(empty_box), lbl, FALSE, FALSE, 0);
        
        gtk_box_pack_start(GTK_BOX(notifications_box), empty_box, TRUE, TRUE, 0);
    } else {
        // Add "Clear History" button at the top
        GtkWidget *clear_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_halign(clear_box, GTK_ALIGN_END);
        gtk_widget_set_margin_bottom(clear_box, 8);
        gtk_widget_set_margin_end(clear_box, 8);
        
        GtkWidget *clear_btn = gtk_button_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(clear_btn), "clear-btn");
        g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_history_clicked), NULL);
        
        GtkWidget *clear_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        
        GtkWidget *clear_icon = gtk_image_new_from_icon_name("edit-delete-symbolic", GTK_ICON_SIZE_MENU);
        gtk_style_context_add_class(gtk_widget_get_style_context(clear_icon), "clear-btn-icon");
        
        GtkWidget *clear_lbl = gtk_label_new("Clear History");
        gtk_style_context_add_class(gtk_widget_get_style_context(clear_lbl), "clear-btn-label");
        
        gtk_box_pack_start(GTK_BOX(clear_btn_box), clear_icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(clear_btn_box), clear_lbl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(clear_btn), clear_btn_box);
        
        gtk_box_pack_start(GTK_BOX(clear_box), clear_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(notifications_box), clear_box, FALSE, FALSE, 0);

        // Build the list
        for (GList *l = history; l != NULL; l = l->next) {
            NotificationData *n = (NotificationData*)l->data;
            
            GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
            gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
            gtk_widget_set_hexpand(card, TRUE);

            // Icon
            GtkWidget *icon;
            if (n->icon_path && strlen(n->icon_path) > 0 && g_file_test(n->icon_path, G_FILE_TEST_EXISTS)) {
                GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(n->icon_path, 32, 32, TRUE, NULL);
                if (pixbuf) {
                    icon = gtk_image_new_from_pixbuf(pixbuf);
                    g_object_unref(pixbuf);
                } else {
                    icon = gtk_image_new_from_icon_name("preferences-system-details", GTK_ICON_SIZE_DND);
                }
            } else {
                icon = gtk_image_new_from_icon_name("preferences-system-details", GTK_ICON_SIZE_DND);
            }
            gtk_widget_set_valign(icon, GTK_ALIGN_START);

            // Text Box
            GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            
            GtkWidget *app_lbl = gtk_label_new(n->app_name);
            gtk_widget_set_halign(app_lbl, GTK_ALIGN_START);
            gtk_style_context_add_class(gtk_widget_get_style_context(app_lbl), "perf-title");

            GtkWidget *sum_lbl = gtk_label_new(n->summary);
            gtk_widget_set_halign(sum_lbl, GTK_ALIGN_START);
            gtk_label_set_line_wrap(GTK_LABEL(sum_lbl), TRUE);
            gtk_style_context_add_class(gtk_widget_get_style_context(sum_lbl), "dnd-title");

            GtkWidget *body_lbl = gtk_label_new(n->body);
            gtk_widget_set_halign(body_lbl, GTK_ALIGN_START);
            gtk_label_set_line_wrap(GTK_LABEL(body_lbl), TRUE);
            gtk_style_context_add_class(gtk_widget_get_style_context(body_lbl), "perf-label");

            gtk_box_pack_start(GTK_BOX(text_box), app_lbl, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(text_box), sum_lbl, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(text_box), body_lbl, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(card), icon, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(card), text_box, TRUE, TRUE, 0);

            gtk_box_pack_start(GTK_BOX(notifications_box), card, FALSE, FALSE, 0);
        }
    }
    gtk_widget_show_all(notifications_box);
}

static void set_tab_active(GtkWidget *active_btn, GtkWidget *inactive_btn) {

    GtkStyleContext *ctx1 = gtk_widget_get_style_context(active_btn);
    GtkStyleContext *ctx2 = gtk_widget_get_style_context(inactive_btn);
    gtk_style_context_add_class(ctx1, "tab-active");
    gtk_style_context_remove_class(ctx2, "tab-active");
}

static void on_tab_controls_clicked(GtkButton *btn, gpointer user_data) {
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "controls");
    set_tab_active(btn_controls, btn_notifications);
}

static void on_tab_notifications_clicked(GtkButton *btn, gpointer user_data) {
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "notifications");
    set_tab_active(btn_notifications, btn_controls);
}

static GtkWidget* create_controls_page() {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(page, 0);

    // ROW 1
    GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_hexpand(row1, TRUE);

    // Top Left Card (Network/BT grid)
    GtkWidget *card_tl_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(card_tl_container), "card");
    gtk_widget_set_hexpand(card_tl_container, TRUE);
    gtk_widget_set_vexpand(card_tl_container, TRUE);
    gtk_widget_set_vexpand(card_tl_container, FALSE);       // ← لا تتمدد عمودياً
    gtk_widget_set_valign(card_tl_container, GTK_ALIGN_FILL); // ← امتلأ بارتفاع الجار
    GtkWidget *card_tl = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(card_tl), 20);
    gtk_grid_set_column_spacing(GTK_GRID(card_tl), 20);
    gtk_widget_set_halign(card_tl, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(card_tl, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(card_tl, TRUE);
    gtk_widget_set_vexpand(card_tl, TRUE);

    GtkWidget *tl_btn1 = create_icon_button("network-wireless-symbolic", NULL);
    GtkWidget *tl_btn2 = create_icon_button("bluetooth-active-symbolic", NULL);
    GtkWidget *tl_btn3 = create_icon_button("network-wired-symbolic", NULL);
    GtkWidget *tl_btn4 = create_icon_button("view-more-symbolic", NULL);

    bt_wifi = tl_btn1;
    bt_eth = tl_btn3;

    gtk_widget_add_events(tl_btn1, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(tl_btn1, "clicked", G_CALLBACK(on_wifi_clicked), NULL);
    g_signal_connect(tl_btn1, "button-press-event", G_CALLBACK(on_wifi_button_press), NULL);
    g_signal_connect(tl_btn3, "clicked", G_CALLBACK(on_eth_clicked), NULL);

    gtk_grid_attach(GTK_GRID(card_tl), tl_btn1, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(card_tl), tl_btn2, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(card_tl), tl_btn3, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(card_tl), tl_btn4, 1, 1, 1, 1);

    gtk_container_add(GTK_CONTAINER(card_tl_container), card_tl);

    // Top Right Container
    GtkWidget *box_tr = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(box_tr, TRUE);

    // Performance Mode Card
    GtkWidget *card_perf = gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
    gtk_style_context_add_class(gtk_widget_get_style_context(card_perf), "card");
    gtk_widget_set_hexpand(card_perf, TRUE);



    GtkWidget *box_perf_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(box_perf_btns, GTK_ALIGN_CENTER);

    const char *perf_icons[] = {"weather-clear-symbolic", "view-dual-symbolic", "system-run-symbolic"};
    const char *perf_labels[] = {"Saver", "Balanced", "Boost"};

    for (int i = 0; i < 3; i++) {
        GtkWidget *btn = gtk_button_new();
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "perf-btn-container");

        GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        
        perf_icons_widgets[i] = gtk_image_new_from_icon_name(perf_icons[i], GTK_ICON_SIZE_BUTTON);
        gtk_style_context_add_class(gtk_widget_get_style_context(perf_icons_widgets[i]), "perf-icon");

        perf_labels_widgets[i] = gtk_label_new(perf_labels[i]);
        gtk_style_context_add_class(gtk_widget_get_style_context(perf_labels_widgets[i]), "perf-label");

        gtk_box_pack_start(GTK_BOX(vb), perf_icons_widgets[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vb), perf_labels_widgets[i], FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(btn), vb);
        
        g_object_set_data(G_OBJECT(btn), "profile", (gpointer)profile_names[i]);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_perf_btn_clicked), NULL);

        gtk_box_pack_start(GTK_BOX(box_perf_btns), btn, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(card_perf), box_perf_btns, FALSE, FALSE, 0);

    // DND Card
    GtkWidget *card_dnd = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(card_dnd), "card");
    gtk_widget_set_valign(card_dnd, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(card_dnd, TRUE);

    GtkWidget *icon_dnd = gtk_image_new_from_icon_name("weather-clear-night-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_dnd), "perf-icon");
    GtkWidget *lbl_dnd = gtk_label_new("DND");
    gtk_label_set_justify(GTK_LABEL(lbl_dnd), GTK_JUSTIFY_LEFT);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_dnd), "dnd-title");

    sw_dnd = gtk_switch_new();
    gtk_widget_set_valign(sw_dnd, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(sw_dnd, GTK_ALIGN_END);
    gtk_widget_set_hexpand(sw_dnd, TRUE);
    g_signal_connect(sw_dnd, "state-set", G_CALLBACK(on_dnd_switch_activated), NULL);

    gtk_box_pack_start(GTK_BOX(card_dnd), icon_dnd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card_dnd), lbl_dnd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card_dnd), sw_dnd, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(box_tr), card_perf, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_tr), card_dnd, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(row1), card_tl_container, FALSE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(row1), box_tr, TRUE, TRUE, 0);

    // ROW 2: Sliders
    GtkWidget *card_sliders = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(card_sliders), "card");

    // Display
    GtkWidget *box_disp_lbl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *icon_disp = gtk_image_new_from_icon_name("video-display-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_disp), "slider-panel-icon");
    GtkWidget *lbl_disp = gtk_label_new("Display");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_disp), "slider-panel-title");
    gtk_box_pack_start(GTK_BOX(box_disp_lbl), icon_disp, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_disp_lbl), lbl_disp, FALSE, FALSE, 0);

    GtkWidget *box_disp_slider = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *icon_sun = gtk_image_new_from_icon_name("display-brightness-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_sun), "slider-panel-icon");
    GtkWidget *scale_disp = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(scale_disp), FALSE);
    gtk_widget_set_hexpand(scale_disp, TRUE);
    gtk_range_set_value(GTK_RANGE(scale_disp), 100);
    gtk_style_context_add_class(gtk_widget_get_style_context(scale_disp), "cyan-scale");
    GtkWidget *lbl_disp_val = gtk_label_new("100%");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_disp_val), "slider-value");
    /* Store references so callbacks can reach them */
    scale_disp_ref    = scale_disp;
    lbl_disp_val_ref  = lbl_disp_val;
    g_signal_connect(scale_disp, "value-changed", G_CALLBACK(on_disp_scale_changed), NULL);

    gtk_box_pack_start(GTK_BOX(box_disp_slider), icon_sun, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_disp_slider), scale_disp, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box_disp_slider), lbl_disp_val, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

    // Sound
    GtkWidget *box_snd_lbl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *icon_snd = gtk_image_new_from_icon_name("audio-volume-high-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_snd), "slider-panel-icon");
    GtkWidget *lbl_snd = gtk_label_new("Sound");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_snd), "slider-panel-title");
    gtk_box_pack_start(GTK_BOX(box_snd_lbl), icon_snd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_snd_lbl), lbl_snd, FALSE, FALSE, 0);

    GtkWidget *snd_spacer = gtk_label_new("");
    gtk_widget_set_hexpand(snd_spacer, TRUE);
    btn_snd_out = create_icon_button("view-more-symbolic", NULL);
    gtk_widget_set_size_request(btn_snd_out, 30, 30);
    g_signal_connect(btn_snd_out, "clicked", G_CALLBACK(on_snd_out_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box_snd_lbl), snd_spacer, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box_snd_lbl), btn_snd_out, FALSE, FALSE, 0);

    GtkWidget *box_snd_slider = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *icon_note = gtk_image_new_from_icon_name("audio-x-generic-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_note), "slider-panel-icon");
    scale_snd = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 150, 1);
    gtk_scale_set_draw_value(GTK_SCALE(scale_snd), FALSE);
    gtk_widget_set_hexpand(scale_snd, TRUE);
    gtk_range_set_value(GTK_RANGE(scale_snd), 130);
    gtk_style_context_add_class(gtk_widget_get_style_context(scale_snd), "cyan-scale");
    lbl_snd_val = gtk_label_new("150%");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_snd_val), "slider-value");

    g_signal_connect(scale_snd, "value-changed", G_CALLBACK(on_snd_scale_changed), NULL);

    gtk_box_pack_start(GTK_BOX(box_snd_slider), icon_note, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_snd_slider), scale_snd, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box_snd_slider), lbl_snd_val, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(card_sliders), box_disp_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card_sliders), box_disp_slider, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card_sliders), sep, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card_sliders), box_snd_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card_sliders), box_snd_slider, FALSE, FALSE, 0);

    // ROW 3: Bottom Media Controls
    GtkWidget *row3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(row3, TRUE);

    GtkWidget *card_media = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_style_context_add_class(gtk_widget_get_style_context(card_media), "card");
    gtk_widget_set_hexpand(card_media, TRUE);

    // Left side of media card: Art + Text
    GtkWidget *media_info_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(media_info_box, TRUE);
    
    m_art_img = gtk_image_new_from_icon_name("audio-x-generic-symbolic", GTK_ICON_SIZE_DIALOG);
    // Force art size to be 64x64 consistently
    gtk_widget_set_size_request(m_art_img, 64, 64);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_art_img), "album-art");

    GtkWidget *media_text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_valign(media_text_box, GTK_ALIGN_CENTER);

    m_title_lbl = gtk_label_new("Not Playing");
    gtk_widget_set_halign(m_title_lbl, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_title_lbl), "app-title");
    gtk_label_set_ellipsize(GTK_LABEL(m_title_lbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(m_title_lbl), 15);

    m_artist_lbl = gtk_label_new("Unknown Artist");
    gtk_widget_set_halign(m_artist_lbl, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_artist_lbl), "perf-label");
    gtk_label_set_ellipsize(GTK_LABEL(m_artist_lbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(m_artist_lbl), 15);

    gtk_box_pack_start(GTK_BOX(media_text_box), m_title_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(media_text_box), m_artist_lbl, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(media_info_box), m_art_img, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(media_info_box), media_text_box, TRUE, TRUE, 0);

    // Right side of media card: Controls
    GtkWidget *media_controls_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(media_controls_box, GTK_ALIGN_END);
    gtk_widget_set_valign(media_controls_box, GTK_ALIGN_CENTER);

    m_btn1 = create_icon_button("media-skip-backward-symbolic", NULL);
    m_btn2 = create_icon_button("media-playback-start-symbolic", NULL);
    m_btn3 = create_icon_button("media-skip-forward-symbolic", NULL);
    
    // Scale down the buttons a little bit to fit nicely
    gtk_widget_set_size_request(m_btn1, 36, 36);
    gtk_widget_set_size_request(m_btn2, 36, 36);
    gtk_widget_set_size_request(m_btn3, 36, 36);
    
    g_signal_connect(m_btn1, "clicked", G_CALLBACK(on_media_prev_clicked), NULL);
    g_signal_connect(m_btn2, "clicked", G_CALLBACK(on_media_play_clicked), NULL);
    g_signal_connect(m_btn3, "clicked", G_CALLBACK(on_media_next_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(media_controls_box), m_btn1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(media_controls_box), m_btn2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(media_controls_box), m_btn3, FALSE, FALSE, 0);

    // Pack into main card
    gtk_box_pack_start(GTK_BOX(card_media), media_info_box, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(card_media), media_controls_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(row3), card_media, TRUE, TRUE, 0);

    // Pack all rows to page
    gtk_box_pack_start(GTK_BOX(page), row1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), card_sliders, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), row3, FALSE, FALSE, 0);

    return page;
}

static GtkWidget* create_notifications_page() {
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);

    notifications_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(notifications_box, 16);
    gtk_widget_set_margin_bottom(notifications_box, 16);
    // Align content nicely around center when empty or list when filled
    gtk_widget_set_valign(notifications_box, GTK_ALIGN_START);
    gtk_widget_set_halign(notifications_box, GTK_ALIGN_FILL);

    gtk_container_add(GTK_CONTAINER(scroll), notifications_box);

    return scroll;
}

GtkWidget* init_control_center(void) {
    const gint panel_offset = 0;
    GtkCssProvider *provider = gtk_css_provider_new();
    GtkWidget *panel_surface;
    char *css_path = panel_resource_path("style.css");
    if (g_file_test(css_path, G_FILE_TEST_EXISTS)) {
        gtk_css_provider_load_from_path(provider, css_path, NULL);
    } else {
        g_warning("Could not find panel style.css at %s", css_path);
    }
    g_free(css_path);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *popover = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(popover, "control-center-popover");
    gtk_window_set_decorated(GTK_WINDOW(popover), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(popover), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(popover), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_layer_init_for_window(GTK_WINDOW(popover));
    gtk_layer_set_namespace(GTK_WINDOW(popover), "vaxpwy-control-center");
    gtk_layer_set_layer(GTK_WINDOW(popover), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(popover), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_margin(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_TOP, panel_offset);
    gtk_layer_set_margin(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
    ensure_rgba_visual(popover);
    
    panel_surface = gtk_event_box_new();
    gtk_widget_set_name(panel_surface, "main-box");
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(panel_surface), TRUE);
    ensure_rgba_visual(panel_surface);
    g_signal_connect(panel_surface, "draw", G_CALLBACK(draw_control_center_background), NULL);
    gtk_container_add(GTK_CONTAINER(popover), panel_surface);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(main_box, "control-center-content");
    gtk_widget_set_size_request(main_box, 360, -1);
    ensure_rgba_visual(main_box);
    
    gtk_widget_set_margin_top(main_box, 8);
    gtk_widget_set_margin_bottom(main_box, 8);
    gtk_widget_set_margin_start(main_box, 8);
    gtk_widget_set_margin_end(main_box, 8);
    gtk_container_add(GTK_CONTAINER(panel_surface), main_box);

    // Header
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(header_box), "header-box");

    // Header Left
    GtkWidget *box_left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *arrow = gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_MENU);
    GtkWidget *title = gtk_label_new("Venom");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "app-title");
    gtk_box_pack_start(GTK_BOX(box_left), arrow, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_left), title, FALSE, FALSE, 0);

    // Header Right
    GtkWidget *box_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(box_right, GTK_ALIGN_END);

    btn_controls = gtk_button_new_with_label("Controls");
    btn_notifications = gtk_button_new_with_label("Notifications");

    gtk_style_context_add_class(gtk_widget_get_style_context(btn_controls), "tab-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_notifications), "tab-button");
    set_tab_active(btn_controls, btn_notifications);

    g_signal_connect(btn_controls, "clicked", G_CALLBACK(on_tab_controls_clicked), NULL);
    g_signal_connect(btn_notifications, "clicked", G_CALLBACK(on_tab_notifications_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(box_right), btn_controls, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_right), btn_notifications, FALSE, FALSE, 0);

    // Pack left and right
    gtk_box_pack_start(GTK_BOX(header_box), box_left, FALSE, FALSE, 0);
    
    GtkWidget *spacer = gtk_label_new("");
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(header_box), spacer, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(header_box), box_right, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), header_box, FALSE, FALSE, 0);

    // Stack
    stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_box_pack_start(GTK_BOX(main_box), stack, TRUE, TRUE, 0);

    GtkWidget *controls_page = create_controls_page();
    GtkWidget *notifications_page = create_notifications_page();

    gtk_stack_add_named(GTK_STACK(stack), controls_page, "controls");
    gtk_stack_add_named(GTK_STACK(stack), notifications_page, "notifications");

    gtk_widget_show_all(main_box);
    gtk_widget_hide(popover);

    pulse_volume_init(on_pulse_volume_changed, NULL);
    power_profile_init(on_power_profile_changed, NULL);
    mpris_control_init(on_mpris_state_changed, NULL);
    venom_notifications_init(on_notifications_updated, on_dnd_state_changed, NULL);
    network_init_state(on_wifi_state_changed, on_eth_state_changed);
    brightness_init(on_brightness_changed, NULL);

    return popover;
}
