#include "wifi_indicator.h"
#include "network_actions.h"
#include "window_backend.h"
#include <gtk-layer-shell.h>
#include <string.h>
#include <stdio.h>

static GtkWidget *wi_btn     = NULL;
static GtkWidget *wi_icon    = NULL;
static GtkWidget *wifi_popup = NULL;
static gboolean   wifi_popup_visible = FALSE;

// Labels in the popup
static GtkWidget *lbl_ssid     = NULL;
static GtkWidget *lbl_strength = NULL;
static GtkWidget *lbl_freq     = NULL;

static const char *wifi_icon_name(int pct)
{
    if (pct <= 0)  return "network-wireless-offline-symbolic";
    if (pct < 33)  return "network-wireless-signal-weak-symbolic";
    if (pct < 66)  return "network-wireless-signal-ok-symbolic";
    if (pct < 90)  return "network-wireless-signal-good-symbolic";
    return              "network-wireless-signal-excellent-symbolic";
}

static void on_wifi_active_changed(WifiActiveInfo *info, gpointer user_data)
{
    (void)user_data;
    if (!wi_icon) return;

    if (!info || !info->ssid || info->ssid[0] == '\0') {
        gtk_image_set_from_icon_name(GTK_IMAGE(wi_icon), "network-wireless-offline-symbolic", GTK_ICON_SIZE_MENU);
        
        if (lbl_ssid) gtk_label_set_text(GTK_LABEL(lbl_ssid), "Not Connected");
        if (lbl_strength) gtk_label_set_text(GTK_LABEL(lbl_strength), "Strength: 0%");
        if (lbl_freq) gtk_label_set_text(GTK_LABEL(lbl_freq), "Band: Unknown");
    } else if (info->is_ethernet) {
        gtk_image_set_from_icon_name(GTK_IMAGE(wi_icon), "network-wired-symbolic", GTK_ICON_SIZE_MENU);

        if (lbl_ssid) gtk_label_set_text(GTK_LABEL(lbl_ssid), info->ssid);

        if (lbl_strength) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Interface: %s",
                     (info->device && info->device[0] != '\0') ? info->device : "Unknown");
            gtk_label_set_text(GTK_LABEL(lbl_strength), buf);
        }

        if (lbl_freq) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Status: %s",
                     (info->connection_name && info->connection_name[0] != '\0') ? info->connection_name : "Connected");
            gtk_label_set_text(GTK_LABEL(lbl_freq), buf);
        }
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(wi_icon), wifi_icon_name(info->strength), GTK_ICON_SIZE_MENU);
        
        if (lbl_ssid) gtk_label_set_text(GTK_LABEL(lbl_ssid), info->ssid);
        
        if (lbl_strength) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Strength: %d%%", info->strength);
            gtk_label_set_text(GTK_LABEL(lbl_strength), buf);
        }
        
        if (lbl_freq) {
            if (info->is_5ghz) {
                gtk_label_set_text(GTK_LABEL(lbl_freq), "Band: 5 GHz");
            } else if (info->frequency > 0) {
                gtk_label_set_text(GTK_LABEL(lbl_freq), "Band: 2.4 GHz");
            } else {
                gtk_label_set_text(GTK_LABEL(lbl_freq), "Band: Unknown");
            }
        }
    }
}

static gboolean on_wifi_popup_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_hide(wifi_popup);
        wifi_popup_visible = FALSE;
        return TRUE;
    }
    return FALSE;
}

static void create_wifi_popup_window(void)
{
    if (wifi_popup) return;

    wifi_popup = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(wifi_popup, "wifi-indicator-window");
    gtk_window_set_decorated(GTK_WINDOW(wifi_popup), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(wifi_popup), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(wifi_popup), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(wifi_popup), TRUE);

    panel_window_backend_init_popup(GTK_WINDOW(wifi_popup),
                                    "vaxpwy-wifi",
                                    GDK_WINDOW_TYPE_HINT_POPUP_MENU,
                                    GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    panel_window_backend_set_anchor(GTK_WINDOW(wifi_popup), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    panel_window_backend_set_anchor(GTK_WINDOW(wifi_popup), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    panel_window_backend_set_margin(GTK_WINDOW(wifi_popup), GTK_LAYER_SHELL_EDGE_TOP, 32);
    panel_window_backend_set_margin(GTK_WINDOW(wifi_popup), GTK_LAYER_SHELL_EDGE_RIGHT, 0);

    GdkScreen  *scr = gtk_widget_get_screen(wifi_popup);
    GdkVisual  *vis = gdk_screen_get_rgba_visual(scr);
    if (vis && gdk_screen_is_composited(scr)) {
        gtk_widget_set_visual(wifi_popup, vis);
        gtk_widget_set_app_paintable(wifi_popup, TRUE);
    }

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(outer, "wifi-popup-outer");
    gtk_container_add(GTK_CONTAINER(wifi_popup), outer);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(header, "wifi-popup-header");

    GtkWidget *hdr_icon = gtk_image_new_from_icon_name("network-wireless-symbolic", GTK_ICON_SIZE_MENU);
    GtkWidget *hdr_lbl  = gtk_label_new("Wi-Fi Status");
    gtk_widget_set_name(hdr_lbl, "wifi-popup-title");

    gtk_box_pack_start(GTK_BOX(header), hdr_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), hdr_lbl,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer),  header,   FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(sep, "wifi-popup-sep");
    gtk_box_pack_start(GTK_BOX(outer), sep, FALSE, FALSE, 0);

    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_name(info_box, "wifi-popup-info");
    gtk_widget_set_margin_top   (info_box, 12);
    gtk_widget_set_margin_bottom(info_box, 12);
    gtk_widget_set_margin_start (info_box, 16);
    gtk_widget_set_margin_end   (info_box, 16);

    lbl_ssid = gtk_label_new("Searching...");
    gtk_widget_set_halign(lbl_ssid, GTK_ALIGN_START);
    gtk_widget_set_name(lbl_ssid, "wifi-info-ssid");

    lbl_strength = gtk_label_new("Strength: 0%");
    gtk_widget_set_halign(lbl_strength, GTK_ALIGN_START);
    gtk_widget_set_name(lbl_strength, "wifi-info-val");

    lbl_freq = gtk_label_new("Band: Unknown");
    gtk_widget_set_halign(lbl_freq, GTK_ALIGN_START);
    gtk_widget_set_name(lbl_freq, "wifi-info-val");

    gtk_box_pack_start(GTK_BOX(info_box), lbl_ssid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info_box), lbl_strength, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info_box), lbl_freq, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(outer), info_box, TRUE, TRUE, 0);

    g_signal_connect(wifi_popup, "key-press-event", G_CALLBACK(on_wifi_popup_key_press), NULL);

    gtk_widget_show_all(outer);
    gtk_widget_hide(wifi_popup);
}

static void on_wi_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    if (!wifi_popup) return;

    if (wifi_popup_visible) {
        gtk_widget_hide(wifi_popup);
        wifi_popup_visible = FALSE;
    } else {
        gtk_widget_show_all(wifi_popup);
        wifi_popup_visible = TRUE;
    }
}

GtkWidget *create_wifi_indicator_widget(void)
{
    wi_btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(wi_btn), GTK_RELIEF_NONE);
    gtk_widget_set_name(wi_btn, "wifi-indicator-btn");

    wi_icon = gtk_image_new_from_icon_name("network-wireless-offline-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(wi_btn), wi_icon);

    g_signal_connect(wi_btn, "clicked", G_CALLBACK(on_wi_clicked), NULL);

    create_wifi_popup_window();

    network_watch_active_wifi(on_wifi_active_changed, NULL);

    return wi_btn;
}
