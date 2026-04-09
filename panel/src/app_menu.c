#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include "app_menu.h"
#include "power_actions.h" // For sleep, restart, shutdown, logout

static gboolean draw_app_menu_background(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    GtkAllocation alloc;
    const double radius = 12.0;

    (void)user_data;
    gtk_widget_get_allocation(widget, &alloc);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.397);
    cairo_new_sub_path(cr);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, alloc.width - radius, 0);
    cairo_arc(cr, alloc.width - radius, radius, radius, -G_PI / 2.0, 0);
    cairo_arc(cr, alloc.width - radius, alloc.height - radius, radius, 0, G_PI / 2.0);
    cairo_arc(cr, radius, alloc.height - radius, radius, G_PI / 2.0, G_PI);
    cairo_line_to(cr, 0, 0);
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

void app_menu_set_relative_to(GtkWidget *menu, GtkWidget *relative_to) {
    (void)menu;
    (void)relative_to;
}

// Callback for menu item clicks
static void on_menu_item_clicked(GtkButton *btn, gpointer user_data) {
    const char *action = (const char *)user_data;
    
    // Hide the menu upon clicking any active item
    GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(btn));
    while (parent && !GTK_IS_WINDOW(parent)) {
        parent = gtk_widget_get_parent(parent);
    }
    
    if (GTK_IS_WINDOW(parent))
        gtk_widget_hide(parent);
    
    // Perform actions
    if (g_strcmp0(action, "sleep") == 0) {
        venom_sleep();
    } else if (g_strcmp0(action, "restart") == 0) {
        venom_reboot();
    } else if (g_strcmp0(action, "shutdown") == 0) {
        venom_power_off();
    } else if (g_strcmp0(action, "logout") == 0) {
        venom_logout();
    } else if (g_strcmp0(action, "store") == 0) {
        GError *error = NULL;
        if (!g_spawn_command_line_async("vstore", &error)) {
            g_print("Failed to launch vstore: %s\n", error->message);
            g_error_free(error);
        }
    } else if (g_strcmp0(action, "about") == 0) {
        GError *error = NULL;
        if (!g_spawn_command_line_async("vsysinfo", &error)) {
            g_print("Failed to launch vsysinfo: %s\n", error->message);
            g_error_free(error);
        }
    } else {
        g_print("Action not implemented yet: %s\n", action);
    }
}

// Helper to create a menu button
static GtkWidget* create_menu_item(const char *label_text, const char *action, const char *shortcut) {
    GtkWidget *btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "app-menu-item");
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    
    GtkWidget *lbl = gtk_label_new(label_text);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), lbl, TRUE, TRUE, 0);
    
    if (shortcut != NULL) {
        GtkWidget *shortcut_lbl = gtk_label_new(shortcut);
        gtk_widget_set_halign(shortcut_lbl, GTK_ALIGN_END);
        gtk_style_context_add_class(gtk_widget_get_style_context(shortcut_lbl), "app-menu-shortcut");
        gtk_box_pack_start(GTK_BOX(box), shortcut_lbl, FALSE, FALSE, 0);
    }
    
    gtk_container_add(GTK_CONTAINER(btn), box);
    
    if (action != NULL) {
        g_signal_connect(btn, "clicked", G_CALLBACK(on_menu_item_clicked), (gpointer)action);
    }
    
    return btn;
}

static GtkWidget* create_separator() {
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_style_context_add_class(gtk_widget_get_style_context(sep), "app-menu-separator");
    gtk_widget_set_margin_top(sep, 4);
    gtk_widget_set_margin_bottom(sep, 4);
    return sep;
}

GtkWidget* init_app_menu(void) {
    const gint panel_offset = 0;
    GtkWidget *popover = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *panel_surface;
    gtk_widget_set_name(popover, "app-menu-popover");
    gtk_window_set_decorated(GTK_WINDOW(popover), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(popover), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(popover), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_layer_init_for_window(GTK_WINDOW(popover));
    gtk_layer_set_namespace(GTK_WINDOW(popover), "vaxpwy-app-menu");
    gtk_layer_set_layer(GTK_WINDOW(popover), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(popover), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_margin(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_TOP, panel_offset);
    gtk_layer_set_margin(GTK_WINDOW(popover), GTK_LAYER_SHELL_EDGE_LEFT, 0);
    ensure_rgba_visual(popover);
    
    panel_surface = gtk_event_box_new();
    gtk_widget_set_name(panel_surface, "app-menu-box");
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(panel_surface), TRUE);
    ensure_rgba_visual(panel_surface);
    g_signal_connect(panel_surface, "draw", G_CALLBACK(draw_app_menu_background), NULL);
    gtk_container_add(GTK_CONTAINER(popover), panel_surface);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(main_box, "app-menu-content");
    ensure_rgba_visual(main_box);
    gtk_widget_set_size_request(main_box, 220, -1);
    
    // Padding for blur / shadow
    gtk_widget_set_margin_top(main_box, 8);
    gtk_widget_set_margin_bottom(main_box, 8);
    gtk_widget_set_margin_start(main_box, 8);
    gtk_widget_set_margin_end(main_box, 8);
    gtk_container_add(GTK_CONTAINER(panel_surface), main_box);

    // Menu Items
    gtk_box_pack_start(GTK_BOX(main_box), create_menu_item("About VAXP", "about", NULL), FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_box), create_menu_item("System Preferences...", "prefs", NULL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), create_menu_item("VAXP Store...", "vstore", NULL), FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_box), create_menu_item("Recent Items", "recent", "›"), FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_box), create_menu_item("Force Quit Vafile", "force_quit", "⌥⇧⌘⎋"), FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_box), create_menu_item("Sleep", "sleep", NULL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), create_menu_item("Restart...", "restart", NULL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), create_menu_item("Shut Down...", "shutdown", NULL), FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_box), create_menu_item("Lock Screen", "logout", "^⌘Q"), FALSE, FALSE, 0);

    gtk_widget_show_all(main_box);
    gtk_widget_hide(popover);

    return popover;
}
