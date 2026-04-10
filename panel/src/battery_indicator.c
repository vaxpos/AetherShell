#include "battery_indicator.h"
#include <gio/gio.h>
#include <stdio.h>

#define UPOWER_BUS "org.freedesktop.UPower"
#define UPOWER_PATH "/org/freedesktop/UPower/devices/DisplayDevice"
#define UPOWER_IFACE "org.freedesktop.UPower.Device"

static GtkWidget *battery_drawing_area;
static GtkWidget *battery_label;
static GDBusProxy *upower_proxy = NULL;

static double current_percentage = 0.0;
static guint32 current_state = 0; // 1=charging, 2=discharging, 3=empty, 4=full, etc.

// Drawing callback for the battery
static gboolean on_draw_battery(GtkWidget *widget, cairo_t *cr, gpointer data) {
    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);

    // Geometry of the battery body
    double bw = 22.0;
    double bh = 10.0;
    double bx = (width - bw - 2.0) / 2.0;
    double by = (height - bh) / 2.0;
    double radius = 2.0;

    // Terminal (the little nub on the right)
    double tw = 2.0;
    double th = 4.0;
    double tx = bx + bw;
    double ty = by + (bh - th) / 2.0;

    // 1. Draw battery body outline (border)
    cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 1.0); // Light gray border
    cairo_set_line_width(cr, 1.0);
    
    // Rounded rectangle for body using arcs
    cairo_new_sub_path(cr);
    cairo_arc(cr, bx + bw - radius, by + radius, radius, -G_PI / 2, 0);
    cairo_arc(cr, bx + bw - radius, by + bh - radius, radius, 0, G_PI / 2);
    cairo_arc(cr, bx + radius, by + bh - radius, radius, G_PI / 2, G_PI);
    cairo_arc(cr, bx + radius, by + radius, radius, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
    cairo_stroke(cr);

    // 2. Draw terminal
    cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 1.0);
    cairo_rectangle(cr, tx, ty, tw, th);
    cairo_fill(cr);

    // 3. Draw inner fill
    // Gap between border and fill
    double gap = 1.0;
    double inner_x = bx + gap + 0.5;
    double inner_y = by + gap + 0.5;
    double inner_max_w = bw - (gap * 2) - 1.0;
    double inner_h = bh - (gap * 2) - 1.0;
    
    double fill_w = inner_max_w * (current_percentage / 100.0);
    if (fill_w < 1.0 && current_percentage > 0) fill_w = 1.0;

    gboolean charging = (current_state == 1 || current_state == 4 || current_state == 5);
    
    // Choose fill color
    if (charging) {
        cairo_set_source_rgba(cr, 0.2, 0.8, 0.2, 1.0); // Green if charging
    } else if (current_percentage <= 20.0) {
        cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, 1.0); // Red if low
    } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0); // White otherwise
    }

    cairo_rectangle(cr, inner_x, inner_y, fill_w, inner_h);
    cairo_fill(cr);

    // If charging, draw a tiny lightning bolt inside or next to it
    if (charging) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.7); // Dark bolt inside
        cairo_move_to(cr, inner_x + inner_max_w/2 - 1, inner_y + 1);
        cairo_line_to(cr, inner_x + inner_max_w/2 - 2, inner_y + inner_h/2);
        cairo_line_to(cr, inner_x + inner_max_w/2, inner_y + inner_h/2);
        cairo_line_to(cr, inner_x + inner_max_w/2 - 1, inner_y + inner_h - 1);
        cairo_stroke(cr);
    }

    return FALSE;
}

static void update_battery_ui(GVariant *properties) {
    if (!properties) return;

    GVariant *v_percent = g_variant_lookup_value(properties, "Percentage", G_VARIANT_TYPE_DOUBLE);
    GVariant *v_state = g_variant_lookup_value(properties, "State", G_VARIANT_TYPE_UINT32);

    gboolean changed = FALSE;

    if (v_percent) {
        double p = g_variant_get_double(v_percent);
        if (p != current_percentage) {
            current_percentage = p;
            changed = TRUE;
        }
        g_variant_unref(v_percent);
    }

    if (v_state) {
        guint32 s = g_variant_get_uint32(v_state);
        if (s != current_state) {
            current_state = s;
            changed = TRUE;
        }
        g_variant_unref(v_state);
    }

    if (changed && battery_drawing_area) {
        gtk_widget_queue_draw(battery_drawing_area);
    }
}

// Called when UPower properties change (e.g., percentage drops)
static void on_upower_properties_changed(GDBusProxy *proxy,
                                         GVariant *changed_properties,
                                         GStrv invalidated_properties,
                                         gpointer user_data) {
    update_battery_ui(changed_properties);
}

// Initial fetch of properties upon starting
static void fetch_initial_battery_state() {
    if (!upower_proxy) return;

    GVariant *result = g_dbus_proxy_call_sync(upower_proxy,
                                              "org.freedesktop.DBus.Properties.GetAll",
                                              g_variant_new("(s)", UPOWER_IFACE),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              NULL);
    if (result) {
        GVariant *props = g_variant_get_child_value(result, 0);
        update_battery_ui(props);
        g_variant_unref(props);
        g_variant_unref(result);
    }
}

static void on_proxy_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    upower_proxy = g_dbus_proxy_new_for_bus_finish(res, &error);

    if (error) {
        g_printerr("Error creating UPower proxy: %s\n", error->message);
        g_error_free(error);
        return;
    }

    // Proxy is ready, listen to signals and fetch initial state
    g_signal_connect(upower_proxy, "g-properties-changed", G_CALLBACK(on_upower_properties_changed), NULL);
    fetch_initial_battery_state();
}

GtkWidget* get_battery_widget(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    // Custom drawing area for the battery
    battery_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(battery_drawing_area, 30, 16);
    g_signal_connect(G_OBJECT(battery_drawing_area), "draw", G_CALLBACK(on_draw_battery), NULL);

    // Fix baseline alignment for perfection
    gtk_widget_set_valign(battery_drawing_area, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(box), battery_drawing_area, FALSE, FALSE, 0);
    
    // Connect to UPower asynchronously
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
