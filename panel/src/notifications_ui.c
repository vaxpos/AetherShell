#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include "venom_notifications.h"
#include "resource_paths.h"
#include "notifications_ui.h"

static GtkWidget *sw_dnd;
static GtkWidget *notifications_box;

static void on_dnd_state_changed(gboolean enabled, gpointer user_data) {
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

static void on_notifications_updated(GList *history, gpointer user_data) {
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(notifications_box));
    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    if (!history) {
        GtkWidget *icon = gtk_image_new_from_icon_name("notifications-disabled-symbolic", GTK_ICON_SIZE_DIALOG);
        gtk_style_context_add_class(gtk_widget_get_style_context(icon), "notification-icon");
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 72);

        GtkWidget *lbl = gtk_label_new("No Notifications");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "notification-label");

        GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);
        gtk_widget_set_vexpand(empty_box, TRUE);
        gtk_widget_set_hexpand(empty_box, TRUE);
        
        gtk_box_pack_start(GTK_BOX(empty_box), icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(empty_box), lbl, FALSE, FALSE, 0);
        
        gtk_box_pack_start(GTK_BOX(notifications_box), empty_box, TRUE, TRUE, 0);
    } else {
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

        for (GList *l = history; l != NULL; l = l->next) {
            NotificationData *n = (NotificationData*)l->data;
            
            GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
            gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
            gtk_widget_set_hexpand(card, TRUE);

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

static GtkWidget* create_notifications_page() {
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);

    notifications_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(notifications_box, 16);
    gtk_widget_set_margin_bottom(notifications_box, 16);
    gtk_widget_set_valign(notifications_box, GTK_ALIGN_START);
    gtk_widget_set_halign(notifications_box, GTK_ALIGN_FILL);

    gtk_container_add(GTK_CONTAINER(scroll), notifications_box);

    return scroll;
}

static gboolean draw_notifications_background(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
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

void notifications_ui_set_relative_to(GtkWidget *notifications_window, GtkWidget *relative_to) {
    (void)notifications_window;
    (void)relative_to;
}

GtkWidget* init_notifications_ui(void) {
    const gint panel_offset = 0;
    
    GtkWidget *popover = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(popover, "notifications-popover");
    gtk_window_set_decorated(GTK_WINDOW(popover), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(popover), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(popover), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_layer_init_for_window(GTK_WINDOW(popover));
    gtk_layer_set_namespace(GTK_WINDOW(popover), "vaxpwy-notifications");
    gtk_layer_set_layer(GTK_WINDOW(popover), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(popover), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_margin(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_TOP, panel_offset);
    gtk_layer_set_margin(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
    ensure_rgba_visual(popover);
    
    GtkWidget *panel_surface = gtk_event_box_new();
    gtk_widget_set_name(panel_surface, "main-box");
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(panel_surface), TRUE);
    ensure_rgba_visual(panel_surface);
    g_signal_connect(panel_surface, "draw", G_CALLBACK(draw_notifications_background), NULL);
    gtk_container_add(GTK_CONTAINER(popover), panel_surface);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(main_box, "control-center-content");
    gtk_widget_set_size_request(main_box, 360, 480);
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
    GtkWidget *title = gtk_label_new("Notifications");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "app-title");
    gtk_box_pack_start(GTK_BOX(box_left), arrow, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_left), title, FALSE, FALSE, 0);

    // Header Right (DND)
    GtkWidget *box_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(box_right, GTK_ALIGN_END);

    GtkWidget *icon_dnd = gtk_image_new_from_icon_name("weather-clear-night-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_dnd), "perf-icon");
    
    sw_dnd = gtk_switch_new();
    gtk_widget_set_valign(sw_dnd, GTK_ALIGN_CENTER);
    g_signal_connect(sw_dnd, "state-set", G_CALLBACK(on_dnd_switch_activated), NULL);

    gtk_box_pack_start(GTK_BOX(box_right), icon_dnd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_right), sw_dnd, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header_box), box_left, FALSE, FALSE, 0);
    
    GtkWidget *spacer = gtk_label_new("");
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(header_box), spacer, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(header_box), box_right, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), header_box, FALSE, FALSE, 0);

    // Notifications Page
    GtkWidget *notifications_page = create_notifications_page();
    gtk_box_pack_start(GTK_BOX(main_box), notifications_page, TRUE, TRUE, 0);

    gtk_widget_show_all(main_box);
    gtk_widget_hide(popover);

    venom_notifications_init(on_notifications_updated, on_dnd_state_changed, NULL);

    return popover;
}
