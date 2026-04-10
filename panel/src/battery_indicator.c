#include "battery_indicator.h"
#include <gio/gio.h>
#include <gtk-layer-shell.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#define UPOWER_BUS "org.freedesktop.UPower"
#define UPOWER_PATH "/org/freedesktop/UPower/devices/DisplayDevice"
#define UPOWER_IFACE "org.freedesktop.UPower.Device"

typedef struct {
    double percentage;
    guint32 state;
    double energy;
    double energy_full;
    double energy_full_design;
    double capacity;
    gint64 update_time;
} BatteryInfo;

static GtkWidget *battery_button;
static GtkWidget *battery_drawing_area;
static GtkWidget *battery_popup;
static GtkWidget *status_value_label;
static GtkWidget *charge_value_label;
static GtkWidget *size_value_label;
static GtkWidget *last_charge_value_label;
static GtkWidget *health_value_label;
static GDBusProxy *upower_proxy = NULL;
static BatteryInfo battery_info = {0};
static gboolean battery_popup_visible = FALSE;

static const char *battery_state_to_text(guint32 state) {
    switch (state) {
        case 1: return "Charging";
        case 2: return "Discharging";
        case 3: return "Empty";
        case 4: return "Full";
        case 5: return "Pending Charge";
        case 6: return "Pending Discharge";
        default: return "Unknown";
    }
}

static void format_energy(double energy_wh, char *out, size_t len) {
    if (energy_wh > 0.0) {
        g_snprintf(out, len, "%.1f Wh", energy_wh);
    } else {
        g_snprintf(out, len, "Unavailable");
    }
}

static void format_timestamp(gint64 timestamp, char *out, size_t len) {
    if (timestamp <= 0) {
        g_snprintf(out, len, "Unavailable");
        return;
    }

    time_t raw = (time_t) timestamp;
    struct tm *tm_info = localtime(&raw);
    if (!tm_info) {
        g_snprintf(out, len, "Unavailable");
        return;
    }

    strftime(out, len, "%Y-%m-%d %H:%M", tm_info);
}

static void update_battery_details_ui(void) {
    char charge_text[64];
    char size_text[64];
    char health_text[64];
    char last_charge_text[64];
    double health_value = 0.0;

    if (status_value_label) {
        gtk_label_set_text(GTK_LABEL(status_value_label), battery_state_to_text(battery_info.state));
    }

    g_snprintf(charge_text, sizeof(charge_text), "%.0f%%", battery_info.percentage);
    if (charge_value_label) {
        gtk_label_set_text(GTK_LABEL(charge_value_label), charge_text);
    }

    format_energy(battery_info.energy_full, size_text, sizeof(size_text));
    if (size_value_label) {
        gtk_label_set_text(GTK_LABEL(size_value_label), size_text);
    }

    if (battery_info.capacity > 0.0) {
        health_value = battery_info.capacity;
    } else if (battery_info.energy_full > 0.0 && battery_info.energy_full_design > 0.0) {
        health_value = (battery_info.energy_full / battery_info.energy_full_design) * 100.0;
    }

    if (health_value > 0.0) {
        g_snprintf(health_text, sizeof(health_text), "%.0f%%", health_value);
    } else {
        g_snprintf(health_text, sizeof(health_text), "Unavailable");
    }
    if (health_value_label) {
        gtk_label_set_text(GTK_LABEL(health_value_label), health_text);
    }

    if (battery_info.state == 1 || battery_info.state == 4 || battery_info.state == 5) {
        format_timestamp(battery_info.update_time, last_charge_text, sizeof(last_charge_text));
    } else {
        g_snprintf(last_charge_text, sizeof(last_charge_text), "Unavailable");
    }
    if (last_charge_value_label) {
        gtk_label_set_text(GTK_LABEL(last_charge_value_label), last_charge_text);
    }

    if (battery_drawing_area) {
        gtk_widget_queue_draw(battery_drawing_area);
    }
}

static gboolean on_draw_battery(GtkWidget *widget, cairo_t *cr, gpointer data) {
    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);
    double current_percentage = battery_info.percentage;
    guint32 current_state = battery_info.state;
    double bw = 22.0;
    double bh = 10.0;
    double bx = (width - bw - 2.0) / 2.0;
    double by = (height - bh) / 2.0;
    double radius = 2.0;
    double tw = 2.0;
    double th = 4.0;
    double tx = bx + bw;
    double ty = by + (bh - th) / 2.0;
    double gap = 1.0;
    double inner_x = bx + gap + 0.5;
    double inner_y = by + gap + 0.5;
    double inner_max_w = bw - (gap * 2) - 1.0;
    double inner_h = bh - (gap * 2) - 1.0;
    double fill_w = inner_max_w * (current_percentage / 100.0);
    gboolean charging = (current_state == 1 || current_state == 4 || current_state == 5);

    (void)data;

    if (fill_w < 1.0 && current_percentage > 0.0) fill_w = 1.0;

    cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, bx + bw - radius, by + radius, radius, -G_PI / 2, 0);
    cairo_arc(cr, bx + bw - radius, by + bh - radius, radius, 0, G_PI / 2);
    cairo_arc(cr, bx + radius, by + bh - radius, radius, G_PI / 2, G_PI);
    cairo_arc(cr, bx + radius, by + radius, radius, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
    cairo_stroke(cr);

    cairo_rectangle(cr, tx, ty, tw, th);
    cairo_fill(cr);

    if (charging) {
        cairo_set_source_rgba(cr, 0.2, 0.8, 0.2, 1.0);
    } else if (current_percentage <= 20.0) {
        cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, 1.0);
    } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    }

    cairo_rectangle(cr, inner_x, inner_y, fill_w, inner_h);
    cairo_fill(cr);

    if (charging) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.7);
        cairo_move_to(cr, inner_x + inner_max_w / 2 - 1, inner_y + 1);
        cairo_line_to(cr, inner_x + inner_max_w / 2 - 2, inner_y + inner_h / 2);
        cairo_line_to(cr, inner_x + inner_max_w / 2, inner_y + inner_h / 2);
        cairo_line_to(cr, inner_x + inner_max_w / 2 - 1, inner_y + inner_h - 1);
        cairo_stroke(cr);
    }

    return FALSE;
}

static void update_battery_ui(GVariant *properties) {
    GVariant *value;

    if (!properties) return;

    value = g_variant_lookup_value(properties, "Percentage", G_VARIANT_TYPE_DOUBLE);
    if (value) {
        battery_info.percentage = g_variant_get_double(value);
        g_variant_unref(value);
    }

    value = g_variant_lookup_value(properties, "State", G_VARIANT_TYPE_UINT32);
    if (value) {
        battery_info.state = g_variant_get_uint32(value);
        g_variant_unref(value);
    }

    value = g_variant_lookup_value(properties, "Energy", G_VARIANT_TYPE_DOUBLE);
    if (value) {
        battery_info.energy = g_variant_get_double(value);
        g_variant_unref(value);
    }

    value = g_variant_lookup_value(properties, "EnergyFull", G_VARIANT_TYPE_DOUBLE);
    if (value) {
        battery_info.energy_full = g_variant_get_double(value);
        g_variant_unref(value);
    }

    value = g_variant_lookup_value(properties, "EnergyFullDesign", G_VARIANT_TYPE_DOUBLE);
    if (value) {
        battery_info.energy_full_design = g_variant_get_double(value);
        g_variant_unref(value);
    }

    value = g_variant_lookup_value(properties, "Capacity", G_VARIANT_TYPE_DOUBLE);
    if (value) {
        battery_info.capacity = g_variant_get_double(value);
        g_variant_unref(value);
    }

    value = g_variant_lookup_value(properties, "UpdateTime", G_VARIANT_TYPE_INT64);
    if (value) {
        battery_info.update_time = g_variant_get_int64(value);
        g_variant_unref(value);
    }

    update_battery_details_ui();
}

static void on_upower_properties_changed(GDBusProxy *proxy,
                                         GVariant *changed_properties,
                                         GStrv invalidated_properties,
                                         gpointer user_data) {
    (void)proxy;
    (void)invalidated_properties;
    (void)user_data;
    update_battery_ui(changed_properties);
}

static void fetch_initial_battery_state(void) {
    GVariant *result;
    GVariant *props;

    if (!upower_proxy) return;

    result = g_dbus_proxy_call_sync(upower_proxy,
                                    "org.freedesktop.DBus.Properties.GetAll",
                                    g_variant_new("(s)", UPOWER_IFACE),
                                    G_DBUS_CALL_FLAGS_NONE,
                                    -1,
                                    NULL,
                                    NULL);
    if (!result) return;

    props = g_variant_get_child_value(result, 0);
    update_battery_ui(props);
    g_variant_unref(props);
    g_variant_unref(result);
}

static GtkWidget *create_info_row(const char *title, GtkWidget **value_label) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *value = gtk_label_new("...");

    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
    gtk_label_set_xalign(GTK_LABEL(value), 1.0);
    gtk_widget_set_hexpand(value, TRUE);

    gtk_box_pack_start(GTK_BOX(row), title_label, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(row), value, FALSE, FALSE, 0);

    if (value_label) *value_label = value;
    return row;
}

static gboolean on_battery_popup_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    (void)user_data;

    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_hide(battery_popup);
        battery_popup_visible = FALSE;
        return TRUE;
    }

    return FALSE;
}

static void create_battery_popup_window(void) {
    GtkWidget *outer;
    GtkWidget *header;
    GtkWidget *header_icon;
    GtkWidget *title = gtk_label_new("Battery Info");
    GtkWidget *separator;
    GtkWidget *content;
    GdkScreen *screen;
    GdkVisual *visual;

    if (battery_popup) return;

    battery_popup = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(battery_popup, "battery-indicator-window");
    gtk_window_set_decorated(GTK_WINDOW(battery_popup), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(battery_popup), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(battery_popup), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(battery_popup), TRUE);

    gtk_layer_init_for_window(GTK_WINDOW(battery_popup));
    gtk_layer_set_namespace(GTK_WINDOW(battery_popup), "vaxpwy-battery");
    gtk_layer_set_layer(GTK_WINDOW(battery_popup), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(battery_popup), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(battery_popup), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(battery_popup), GTK_LAYER_SHELL_EDGE_TOP, 32);
    gtk_layer_set_margin(GTK_WINDOW(battery_popup), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(battery_popup), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    screen = gtk_widget_get_screen(battery_popup);
    visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(battery_popup, visual);
        gtk_widget_set_app_paintable(battery_popup, TRUE);
    }

    outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(outer, "battery-popup-outer");
    gtk_container_add(GTK_CONTAINER(battery_popup), outer);

    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(header, "battery-popup-header");
    header_icon = gtk_image_new_from_icon_name("battery-symbolic", GTK_ICON_SIZE_MENU);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_pack_start(GTK_BOX(header), header_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), header, FALSE, FALSE, 0);

    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(separator, "battery-popup-sep");
    gtk_box_pack_start(GTK_BOX(outer), separator, FALSE, FALSE, 0);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_name(content, "battery-popup-info");
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_pack_start(GTK_BOX(content), create_info_row("Status", &status_value_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), create_info_row("Charge", &charge_value_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), create_info_row("Battery Size", &size_value_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), create_info_row("Last Charge", &last_charge_value_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), create_info_row("Battery Health", &health_value_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), content, TRUE, TRUE, 0);

    g_signal_connect(battery_popup, "key-press-event", G_CALLBACK(on_battery_popup_key_press), NULL);
    gtk_widget_show_all(outer);
    gtk_widget_hide(battery_popup);
}

static void on_battery_button_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    update_battery_details_ui();
    if (!battery_popup) return;

    if (battery_popup_visible) {
        gtk_widget_hide(battery_popup);
        battery_popup_visible = FALSE;
    } else {
        gtk_widget_show_all(battery_popup);
        battery_popup_visible = TRUE;
    }
}

static void on_proxy_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;

    (void)source_object;
    (void)user_data;
    upower_proxy = g_dbus_proxy_new_for_bus_finish(res, &error);

    if (error) {
        g_printerr("Error creating UPower proxy: %s\n", error->message);
        g_error_free(error);
        return;
    }

    g_signal_connect(upower_proxy, "g-properties-changed", G_CALLBACK(on_upower_properties_changed), NULL);
    fetch_initial_battery_state();
}

GtkWidget *get_battery_widget(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    battery_button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(battery_button), GTK_RELIEF_NONE);
    gtk_widget_set_margin_start(battery_button, 8);

    battery_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(battery_drawing_area, 30, 16);
    gtk_widget_set_valign(battery_drawing_area, GTK_ALIGN_CENTER);
    g_signal_connect(battery_drawing_area, "draw", G_CALLBACK(on_draw_battery), NULL);

    gtk_container_add(GTK_CONTAINER(battery_button), battery_drawing_area);
    gtk_box_pack_start(GTK_BOX(box), battery_button, FALSE, FALSE, 0);

    create_battery_popup_window();
    g_signal_connect(battery_button, "clicked", G_CALLBACK(on_battery_button_clicked), NULL);

    g_dbus_proxy_new_for_bus(G_BUS_TYPE_SYSTEM,
                             G_DBUS_PROXY_FLAGS_NONE,
                             NULL,
                             UPOWER_BUS,
                             UPOWER_PATH,
                             UPOWER_IFACE,
                             NULL,
                             on_proxy_ready,
                             NULL);

    return box;
}
