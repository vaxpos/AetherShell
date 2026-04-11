/*
 * bt_indicator.c
 *
 * Bluetooth status icon in the panel status-box.
 *  - Icon reflects adapter state (off / on / device connected).
 *  - Left-click  → toggle adapter Powered.
 *  - Popup shows the currently connected device name.
 */

#include "bt_indicator.h"
#include "bluetooth_manager.h"
#include "window_backend.h"
#include <gtk-layer-shell.h>
#include <string.h>

static GtkWidget *bti_btn    = NULL;
static GtkWidget *bti_icon   = NULL;
static GtkWidget *bt_popup   = NULL;
static GtkWidget *bt_popup_lbl_device = NULL;
static GtkWidget *bt_popup_lbl_status = NULL;
static gboolean   bt_popup_visible = FALSE;

/* ── Icon selection ──────────────────────────────────────────────────────── */

static void update_bt_icon(gboolean powered, gboolean connected)
{
    if (!bti_icon) return;
    const gchar *icon;
    if (!powered)
        icon = "bluetooth-disabled-symbolic";
    else if (connected)
        icon = "bluetooth-paired-symbolic";
    else
        icon = "bluetooth-active-symbolic";
    gtk_image_set_from_icon_name(GTK_IMAGE(bti_icon), icon, GTK_ICON_SIZE_MENU);
}

/* ── State callback from bluetooth_manager ───────────────────────────────── */

static void on_bt_state_changed(gboolean powered, gpointer user_data)
{
    (void)user_data;
    BtDevice *dev = powered ? bluetooth_get_connected_device() : NULL;
    update_bt_icon(powered, dev != NULL);

    if (bt_popup_lbl_status) {
        gtk_label_set_text(GTK_LABEL(bt_popup_lbl_status),
                           powered ? "Bluetooth: On" : "Bluetooth: Off");
    }
    if (bt_popup_lbl_device) {
        gtk_label_set_text(GTK_LABEL(bt_popup_lbl_device),
                           (dev && dev->name) ? dev->name : "No device connected");
    }
    bluetooth_device_free(dev);
}

/* ── Popup window ────────────────────────────────────────────────────────── */

static void create_bt_popup(void)
{
    if (bt_popup) return;

    bt_popup = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(bt_popup, "bt-indicator-window");
    gtk_window_set_decorated(GTK_WINDOW(bt_popup), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(bt_popup), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(bt_popup), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(bt_popup), TRUE);

    panel_window_backend_init_popup(GTK_WINDOW(bt_popup),
                                    "vaxpwy-bt",
                                    GDK_WINDOW_TYPE_HINT_POPUP_MENU,
                                    GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    panel_window_backend_set_anchor(GTK_WINDOW(bt_popup), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    panel_window_backend_set_anchor(GTK_WINDOW(bt_popup), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    panel_window_backend_set_margin(GTK_WINDOW(bt_popup), GTK_LAYER_SHELL_EDGE_TOP, 32);
    panel_window_backend_set_margin(GTK_WINDOW(bt_popup), GTK_LAYER_SHELL_EDGE_RIGHT, 0);

    GdkScreen *scr = gtk_widget_get_screen(bt_popup);
    GdkVisual *vis = gdk_screen_get_rgba_visual(scr);
    if (vis && gdk_screen_is_composited(scr)) {
        gtk_widget_set_visual(bt_popup, vis);
        gtk_widget_set_app_paintable(bt_popup, TRUE);
    }

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(outer, "wifi-popup-outer");   /* reuse wifi popup style */
    gtk_container_add(GTK_CONTAINER(bt_popup), outer);

    /* Header */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(header, "wifi-popup-header");
    GtkWidget *hdr_icon = gtk_image_new_from_icon_name("bluetooth-active-symbolic", GTK_ICON_SIZE_MENU);
    GtkWidget *hdr_lbl  = gtk_label_new("Bluetooth");
    gtk_widget_set_name(hdr_lbl, "wifi-popup-title");
    gtk_box_pack_start(GTK_BOX(header), hdr_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), hdr_lbl,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer),  header,   FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(sep, "wifi-popup-sep");
    gtk_box_pack_start(GTK_BOX(outer), sep, FALSE, FALSE, 0);

    /* Info area */
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_name(info_box, "wifi-popup-info");
    gtk_widget_set_margin_top   (info_box, 12);
    gtk_widget_set_margin_bottom(info_box, 12);
    gtk_widget_set_margin_start (info_box, 16);
    gtk_widget_set_margin_end   (info_box, 16);

    bt_popup_lbl_status = gtk_label_new("Bluetooth: Off");
    gtk_widget_set_halign(bt_popup_lbl_status, GTK_ALIGN_START);
    gtk_widget_set_name(bt_popup_lbl_status, "wifi-info-ssid");

    bt_popup_lbl_device = gtk_label_new("No device connected");
    gtk_widget_set_halign(bt_popup_lbl_device, GTK_ALIGN_START);
    gtk_widget_set_name(bt_popup_lbl_device, "wifi-info-val");

    gtk_box_pack_start(GTK_BOX(info_box), bt_popup_lbl_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info_box), bt_popup_lbl_device, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer),    info_box,            TRUE,  TRUE,  0);

    gtk_widget_show_all(outer);
    gtk_widget_hide(bt_popup);
}

/* ── Click handler ───────────────────────────────────────────────────────── */

static void on_bti_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    if (!bt_popup) return;

    if (bt_popup_visible) {
        gtk_widget_hide(bt_popup);
        bt_popup_visible = FALSE;
    } else {
        /* Refresh info before showing */
        gboolean powered = bluetooth_is_powered();
        BtDevice *dev = powered ? bluetooth_get_connected_device() : NULL;
        if (bt_popup_lbl_status)
            gtk_label_set_text(GTK_LABEL(bt_popup_lbl_status),
                               powered ? "Bluetooth: On" : "Bluetooth: Off");
        if (bt_popup_lbl_device)
            gtk_label_set_text(GTK_LABEL(bt_popup_lbl_device),
                               (dev && dev->name) ? dev->name : "No device connected");
        bluetooth_device_free(dev);

        gtk_widget_show_all(bt_popup);
        bt_popup_visible = TRUE;
    }
}

/* ── Key press (Escape to close) ─────────────────────────────────────────── */

static gboolean on_bt_popup_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    (void)user_data;
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_hide(widget);
        bt_popup_visible = FALSE;
        return TRUE;
    }
    return FALSE;
}

/* ── Public factory ──────────────────────────────────────────────────────── */

GtkWidget *create_bt_indicator_widget(void)
{
    bti_btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(bti_btn), GTK_RELIEF_NONE);
    gtk_widget_set_name(bti_btn, "bt-indicator-btn");

    bti_icon = gtk_image_new_from_icon_name("bluetooth-disabled-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(bti_btn), bti_icon);

    g_signal_connect(bti_btn, "clicked", G_CALLBACK(on_bti_clicked), NULL);

    create_bt_popup();
    if (bt_popup)
        g_signal_connect(bt_popup, "key-press-event", G_CALLBACK(on_bt_popup_key_press), NULL);

    /* Init BlueZ — state callback will set the correct icon immediately */
    bluetooth_init(on_bt_state_changed, NULL);

    return bti_btn;
}
