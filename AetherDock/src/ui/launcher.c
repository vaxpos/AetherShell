#include "launcher.h"
#include "search.h"
#include "pager.h"
#include "utils.h"
#include "logic/app_manager.h"
#include <gio/gdesktopappinfo.h>
#include <gdk/gdkwayland.h>
#include <stdlib.h>
#include <string.h>

/* Global variables */
GtkWidget *launcher_button = NULL;
GtkWidget *launcher_window = NULL;
GtkWidget *app_stack = NULL;
GtkWidget *search_entry = NULL;
GtkWidget *search_results_view = NULL;
GtkWidget *prev_button = NULL;
GtkWidget *next_button = NULL;
int total_pages = 0;
static gboolean is_standalone = FALSE;
static gboolean launcher_is_wayland_session(void) {
    return GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default());
}

/* Prototypes */
void on_launcher_clicked(GtkWidget *widget, gpointer data);
void on_launcher_app_clicked(GtkWidget *widget, gpointer data);

/* Standalone Entry */
void launcher_start_standalone(void) {
    is_standalone = TRUE;
    /* Simulate click to open window initially ONLY if not toggled via DBus (handled in main) 
       Actually, standard behavior is startup = show. */
    on_launcher_clicked(NULL, NULL);
}

void launcher_toggle_visibility(void) {
    on_launcher_clicked(NULL, NULL);
}
void populate_applications_grid(GtkWidget *stack);
gboolean on_launcher_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);
gboolean on_launcher_key_press(GtkWidget *window, GdkEventKey *event, gpointer data);
void on_search_activate(GtkEntry *entry, gpointer data);
void on_search_changed(GtkSearchEntry *entry, gpointer data);
void update_navigation_buttons(void);

/* ... Navigation buttons logic (retained) ... */
void update_navigation_buttons(void) {
    if (!app_stack || !prev_button || !next_button) return;
    
    GtkWidget *visible_child = gtk_stack_get_visible_child(GTK_STACK(app_stack));
    if (!visible_child) return;
    
    const gchar *name = gtk_widget_get_name(visible_child);
    if (g_strcmp0(name, "search_results_scroll") == 0) {
        gtk_widget_hide(prev_button);
        gtk_widget_hide(next_button);
        return;
    }
    
    const gchar *child_name = gtk_stack_get_visible_child_name(GTK_STACK(app_stack));
    if (child_name && g_str_has_prefix(child_name, "page")) {
        int page_num = atoi(child_name + 4);
        if (page_num > 0) gtk_widget_show(prev_button);
        else gtk_widget_hide(prev_button);
        
        if (page_num < total_pages - 1) gtk_widget_show(next_button);
        else gtk_widget_hide(next_button);
    } else {
        gtk_widget_hide(prev_button);
        gtk_widget_hide(next_button);
    }
}

void on_stack_visible_child_notify(GObject *gobject, GParamSpec *pspec, gpointer user_data) {
    (void)gobject; (void)pspec; (void)user_data;
    update_navigation_buttons();
}

/* Use Search Logic from UI/Search */
void on_search_activate(GtkEntry *entry, gpointer data) {
    (void)data;
    const gchar *text = gtk_entry_get_text(entry);
    perform_search(text, app_stack, search_results_view, launcher_window);
}

void on_search_changed(GtkSearchEntry *entry, gpointer data) {
    (void)data;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
    perform_search(text, app_stack, search_results_view, launcher_window);
}

void on_launcher_app_clicked(GtkWidget *widget, gpointer data) {
    const gchar *type = (const gchar *)data;
    
    if (g_strcmp0(type, "app") == 0) {
        const gchar *desktop_file = g_object_get_data(G_OBJECT(widget), "desktop-file");
        if (desktop_file != NULL) {
            GError *error = NULL;
            if (!app_mgr_launch(desktop_file, &error)) {
                g_warning("Failed to launch app: %s", error->message);
                g_error_free(error);
            }
            
            /* Close launcher */
            if (launcher_window) {
                gtk_widget_destroy(launcher_window);
                launcher_window = NULL;
                app_stack = NULL;
                /* Reset pointers */
                search_entry = NULL;
                search_results_view = NULL;
                next_button = NULL;
            }
            /* Keep running for DBus */
            // if (is_standalone) gtk_main_quit();
        }
    }
}

void on_prev_page_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    GtkStack *stack = GTK_STACK(data);
    GtkWidget *visible = gtk_stack_get_visible_child(stack);
    if (visible) {
        if (g_strcmp0(gtk_widget_get_name(visible), "search_results_scroll") == 0) return;
        GList *children = gtk_container_get_children(GTK_CONTAINER(stack));
        GList *l = g_list_find(children, visible);
        if (l && l->prev) gtk_stack_set_visible_child(stack, GTK_WIDGET(l->prev->data));
        g_list_free(children);
    }
}

void on_next_page_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    GtkStack *stack = GTK_STACK(data);
    GtkWidget *visible = gtk_stack_get_visible_child(stack);
    if (visible) {
        if (g_strcmp0(gtk_widget_get_name(visible), "search_results_scroll") == 0) return;
        GList *children = gtk_container_get_children(GTK_CONTAINER(stack));
        GList *l = g_list_find(children, visible);
        if (l && l->next) gtk_stack_set_visible_child(stack, GTK_WIDGET(l->next->data));
        g_list_free(children);
    }
}

/* Callback from Pager to close launcher */
static void on_pager_element_clicked(int desktop_idx, gpointer user_data) {
    (void)desktop_idx; (void)user_data;
    if (launcher_window) {
        gtk_widget_destroy(launcher_window);
        launcher_window = NULL;
        app_stack = NULL;
        search_entry = NULL;
        search_results_view = NULL;
        prev_button = NULL;
        next_button = NULL;
    }
}

/* Launcher button clicked - show applications grid */
void on_launcher_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    
    if (!is_standalone) {
        GError *error = NULL;
        gchar *argv[] = {"aether-launcher", NULL};
        if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
             g_warning("Failed to spawn launcher: %s", error->message);
             g_error_free(error);
        }
        return;
    }
    
    if (launcher_window != NULL) {
        gtk_widget_destroy(launcher_window);
        launcher_window = NULL;
        app_stack = NULL;
        search_entry = NULL;
        search_results_view = NULL;
        prev_button = NULL;
        next_button = NULL;
        return;
    }
    launcher_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    
    /* Visual Setup */
    GdkScreen *l_screen = gtk_window_get_screen(GTK_WINDOW(launcher_window));
    GdkVisual *l_visual = gdk_screen_get_rgba_visual(l_screen);
    if (l_visual != NULL && gdk_screen_is_composited(l_screen)) {
        gtk_widget_set_visual(launcher_window, l_visual);
    }
    
    gtk_widget_realize(launcher_window);
    if (!launcher_is_wayland_session()) {
        gdk_window_set_override_redirect(gtk_widget_get_window(launcher_window), TRUE);
    }
    
    gtk_window_set_decorated(GTK_WINDOW(launcher_window), FALSE);
    gtk_widget_set_name(launcher_window, "launcher-window");
    gtk_widget_set_app_paintable(launcher_window, TRUE);
    
    /* Full Screen Size via GdkMonitor (non-deprecated) */
    GdkDisplay *l_display = gdk_screen_get_display(l_screen);
    GdkMonitor *l_monitor = gdk_display_get_primary_monitor(l_display);
    GdkRectangle l_geom;
    gdk_monitor_get_geometry(l_monitor, &l_geom);
    gint screen_width  = l_geom.width;
    gint screen_height = l_geom.height;
    gtk_window_set_default_size(GTK_WINDOW(launcher_window), screen_width, screen_height);
    gtk_window_move(GTK_WINDOW(launcher_window), l_geom.x, l_geom.y);
    
    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(root_box), 40);
    gtk_container_add(GTK_CONTAINER(launcher_window), root_box);
    
    search_entry = gtk_search_entry_new();
    gtk_widget_set_size_request(search_entry, 400, -1);
    gtk_widget_set_halign(search_entry, GTK_ALIGN_CENTER);
    g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_search_changed), NULL);
    g_signal_connect(search_entry, "activate", G_CALLBACK(on_search_activate), NULL);
    gtk_box_pack_start(GTK_BOX(root_box), search_entry, FALSE, FALSE, 0);
    
    /* Pager Injection */
    if (!launcher_is_wayland_session()) {
        pager_init();
        
        GtkWidget *pager = pager_create_widget();
        gtk_widget_set_halign(pager, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_top(pager, 10);
        gtk_widget_set_margin_bottom(pager, 10);
        
        pager_set_click_callback(on_pager_element_clicked, NULL);
        gtk_box_pack_start(GTK_BOX(root_box), pager, FALSE, FALSE, 0);
        pager_update();
    }

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_halign(main_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(main_box, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(root_box), main_box, TRUE, TRUE, 0);

    app_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(app_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    
    /* Search Results Scroll */
    GtkWidget *results_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_name(results_scroll, "search_results_scroll");
    search_results_view = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(search_results_view, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(results_scroll), search_results_view);
    gtk_stack_add_named(GTK_STACK(app_stack), results_scroll, "search_results");
    
    populate_applications_grid(app_stack);
    
    prev_button = gtk_button_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_name(prev_button, "nav-button");
    gtk_widget_set_valign(prev_button, GTK_ALIGN_CENTER);
    g_signal_connect(prev_button, "clicked", G_CALLBACK(on_prev_page_clicked), app_stack);
    gtk_box_pack_start(GTK_BOX(main_box), prev_button, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_box), app_stack, TRUE, TRUE, 0);
    
    next_button = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_name(next_button, "nav-button");
    gtk_widget_set_valign(next_button, GTK_ALIGN_CENTER);
    g_signal_connect(next_button, "clicked", G_CALLBACK(on_next_page_clicked), app_stack);
    gtk_box_pack_start(GTK_BOX(main_box), next_button, FALSE, FALSE, 0);
    
    g_signal_connect(launcher_window, "key-press-event", G_CALLBACK(on_launcher_key_press), NULL);
    g_signal_connect(launcher_window, "delete-event", G_CALLBACK(on_launcher_delete_event), NULL);
    g_signal_connect(app_stack, "notify::visible-child", G_CALLBACK(on_stack_visible_child_notify), NULL);
    
    gtk_widget_show_all(launcher_window);
    
    update_navigation_buttons();
    GtkWidget *first = gtk_stack_get_child_by_name(GTK_STACK(app_stack), "page0");
    if (first) gtk_stack_set_visible_child(GTK_STACK(app_stack), first);
    
    gtk_window_present(GTK_WINDOW(launcher_window));
    gtk_widget_grab_focus(search_entry);
    
    /* Grab Input */
}

void create_launcher_button(GtkWidget *box) {
    launcher_button = gtk_button_new();
    gtk_widget_set_name(launcher_button, "launcher-button");
    
    GError *err = NULL;
    gchar *launcher_icon_path = dock_build_resource_path("launcher.svg");
    GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(launcher_icon_path, 34, 34, TRUE, &err);
    if (!pix) {
        if (err) g_error_free(err);
        gtk_container_add(GTK_CONTAINER(launcher_button), gtk_image_new_from_icon_name("start-here-symbolic", GTK_ICON_SIZE_BUTTON));
    } else {
        gtk_container_add(GTK_CONTAINER(launcher_button), gtk_image_new_from_pixbuf(pix));
        g_object_unref(pix);
    }
    g_free(launcher_icon_path);
    
    g_signal_connect(launcher_button, "clicked", G_CALLBACK(on_launcher_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(box), launcher_button, FALSE, FALSE, 0);
    gtk_widget_show(launcher_button);
}

gboolean on_launcher_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    (void)widget; (void)event; (void)data;
    if (launcher_window) {
        gtk_widget_destroy(launcher_window);
        launcher_window = NULL;
        app_stack = NULL;
        search_entry = NULL;
        search_results_view = NULL;
        prev_button = NULL;
        next_button = NULL;
    }
    /* Do NOT quit main loop in standalone mode, just hide/destroy window to wait for next DBus signal */
    // if (is_standalone) gtk_main_quit(); 
    return TRUE;
}

gboolean on_launcher_key_press(GtkWidget *window, GdkEventKey *event, gpointer data) {
    (void)window; (void)data;
    if (event->keyval == GDK_KEY_Escape) {
        if (launcher_window) {
            gtk_widget_destroy(launcher_window);
            launcher_window = NULL;
            app_stack = NULL;
            search_entry = NULL;
            search_results_view = NULL;
            prev_button = NULL;
            next_button = NULL;
        }
        /* Do NOT quit main loop */
        // if (is_standalone) gtk_main_quit();
        return TRUE;
    }
    return FALSE;
}

void populate_applications_grid(GtkWidget *stack) {
    GList *apps = app_mgr_scan_apps();
    
    int app_count = 0;
    int page_count = 0;
    GtkWidget *current_grid = NULL;
    
    for (GList *l = apps; l != NULL; l = l->next) {
        AppInfo *info = (AppInfo *)l->data;
        
        if (app_count % 24 == 0) {
            current_grid = gtk_grid_new();
            gtk_grid_set_row_spacing(GTK_GRID(current_grid), 20);
            gtk_grid_set_column_spacing(GTK_GRID(current_grid), 20);
            gtk_widget_set_halign(current_grid, GTK_ALIGN_CENTER);
            gtk_widget_set_valign(current_grid, GTK_ALIGN_CENTER);
            
            gchar *page_name = g_strdup_printf("page%d", page_count++);
            gtk_stack_add_named(GTK_STACK(stack), current_grid, page_name);
            g_free(page_name);
        }
        
        GtkWidget *app_btn = gtk_button_new();
        gtk_widget_set_name(app_btn, "app-grid-button");
        gtk_widget_set_size_request(app_btn, 120, 120);
        
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        
        GtkWidget *image;
        if (info->pixbuf) {
            /* Use cached pixbuf (ref it) */
            image = gtk_image_new_from_pixbuf(info->pixbuf);
        } else {
            /* Should be unreachable with new app_manager hardening */
            image = gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_DND);
        }
        gtk_box_pack_start(GTK_BOX(box), image, TRUE, TRUE, 0);
        
        GtkWidget *label = gtk_label_new(info->name);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 10);
        gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
        
        gtk_container_add(GTK_CONTAINER(app_btn), box);
        g_object_set_data_full(G_OBJECT(app_btn), "desktop-file", g_strdup(info->desktop_file_path), g_free);
        g_signal_connect(app_btn, "clicked", G_CALLBACK(on_launcher_app_clicked), (gpointer)"app");
        
        int pos = app_count % 24;
        gtk_grid_attach(GTK_GRID(current_grid), app_btn, pos % 6, pos / 6, 1, 1);
        
        app_count++;
    }
    
    app_mgr_free_list(apps);
    total_pages = page_count;
}
