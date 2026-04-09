/*
 * selection.c
 * Selection state, rubber-band drawing, and desktop background menu.
 */

#include "selection.h"
#include "desktop_config.h"
#include "icons.h"
#include "menu.h"
#include "wallpaper.h"
#include "widgets_manager.h"
#include <math.h>

double start_x = 0;
double start_y = 0;
double current_x = 0;
double current_y = 0;
gboolean is_selecting = FALSE;
GList *selected_items = NULL;

gboolean is_selected(GtkWidget *item) {
    return g_list_find(selected_items, item) != NULL;
}

void select_item(GtkWidget *item) {
    if (!is_selected(item)) {
        GtkStyleContext *context;
        selected_items = g_list_append(selected_items, item);
        context = gtk_widget_get_style_context(item);
        gtk_style_context_add_class(context, "selected");
        gtk_widget_queue_draw(item);
    }
}

void deselect_item(GtkWidget *item) {
    GList *l = g_list_find(selected_items, item);
    if (l) {
        GtkStyleContext *context = gtk_widget_get_style_context(item);
        selected_items = g_list_delete_link(selected_items, l);
        gtk_style_context_remove_class(context, "selected");
        gtk_widget_queue_draw(item);
    }
}

void deselect_all(void) {
    for (GList *l = selected_items; l != NULL; l = l->next) {
        GtkWidget *item = GTK_WIDGET(l->data);
        if (GTK_IS_WIDGET(item)) {
            GtkStyleContext *context = gtk_widget_get_style_context(item);
            gtk_style_context_remove_class(context, "selected");
            gtk_widget_queue_draw(item);
        }
    }
    g_list_free(selected_items);
    selected_items = NULL;
}

gboolean on_layout_draw_fg(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)widget;
    (void)data;

    if (is_selecting) {
        double x = MIN(start_x, current_x);
        double y = MIN(start_y, current_y);
        double w = fabs(current_x - start_x);
        double h = fabs(current_y - start_y);

        cairo_set_source_rgba(cr, 0.2, 0.6, 1.0, 0.3);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 0.2, 0.6, 1.0, 0.8);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, x, y, w, h);
        cairo_stroke(cr);
    }

    return FALSE;
}

gboolean on_bg_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)data;

    if (event->button == 1) {
        deselect_all();
        is_selecting = TRUE;
        start_x = event->x;
        start_y = event->y;
        current_x = event->x;
        current_y = event->y;
        gtk_widget_queue_draw(icon_layout);
        return TRUE;
    }

    if (event->button == 3) {
        GtkWidget *menu = gtk_menu_new();
        GtkWidget *new_folder = gtk_menu_item_new_with_label("Create Folder");
        GtkWidget *create_doc = gtk_menu_item_new_with_label("📄 Create Document");
        GtkWidget *term = gtk_menu_item_new_with_label("Open Terminal Here");
        GtkWidget *paste = gtk_menu_item_new_with_label("Paste");
        GtkWidget *sort_item = gtk_menu_item_new_with_label("Sort By");
        GtkWidget *sort_menu = gtk_menu_new();
        GtkWidget *refresh = gtk_menu_item_new_with_label("Refresh");
        GtkWidget *quit = gtk_menu_item_new_with_label("Quit Desktop");
        GtkWidget *templates_sub = build_templates_submenu();
        GtkWidget *wallpaper = gtk_menu_item_new_with_label("🖼 Change Wallpaper");
        GtkWidget *mode_item = gtk_menu_item_new_with_label("💻 Desktop Mode");
        GtkWidget *mode_menu = gtk_menu_new();
        GtkWidget *m_normal = gtk_menu_item_new_with_label("Normal (Desktop)");
        GtkWidget *m_work = gtk_menu_item_new_with_label("Work (Work)");
        GtkWidget *m_widgets = gtk_menu_item_new_with_label("Widgets Only");
        GtkWidget *s_manual = gtk_menu_item_new_with_label("Manual");
        GtkWidget *s_name = gtk_menu_item_new_with_label("Name");
        GtkWidget *s_type = gtk_menu_item_new_with_label("Type");
        GtkWidget *s_date = gtk_menu_item_new_with_label("Date Modified");
        GtkWidget *s_size = gtk_menu_item_new_with_label("Size");
        GtkWidget *edit_widgets = gtk_menu_item_new_with_label("🧩 Edit Widgets");
        DesktopMode current_mode = get_current_desktop_mode();

        style_context_menu(menu);
        style_context_menu(sort_menu);
        style_context_menu(mode_menu);

        gtk_menu_item_set_submenu(GTK_MENU_ITEM(create_doc), templates_sub);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mode_item), mode_menu);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(sort_item), sort_menu);

        g_signal_connect(new_folder, "activate", G_CALLBACK(on_create_folder), widget);
        g_signal_connect(term, "activate", G_CALLBACK(on_open_terminal), NULL);
        g_signal_connect(paste, "activate", G_CALLBACK(on_bg_paste), NULL);
        g_signal_connect(refresh, "activate", G_CALLBACK(on_refresh_clicked), NULL);
        g_signal_connect(quit, "activate", G_CALLBACK(gtk_main_quit), NULL);
        g_signal_connect(wallpaper, "activate", G_CALLBACK(on_change_wallpaper), widget);
        g_signal_connect(m_normal, "activate", G_CALLBACK(on_mode_normal), NULL);
        g_signal_connect(m_work, "activate", G_CALLBACK(on_mode_work), NULL);
        g_signal_connect(m_widgets, "activate", G_CALLBACK(on_mode_widgets), NULL);
        g_signal_connect(s_manual, "activate", G_CALLBACK(on_sort_mode_selected), GINT_TO_POINTER(SORT_MANUAL));
        g_signal_connect(s_name, "activate", G_CALLBACK(on_sort_mode_selected), GINT_TO_POINTER(SORT_NAME));
        g_signal_connect(s_type, "activate", G_CALLBACK(on_sort_mode_selected), GINT_TO_POINTER(SORT_TYPE));
        g_signal_connect(s_date, "activate", G_CALLBACK(on_sort_mode_selected), GINT_TO_POINTER(SORT_DATE_MODIFIED));
        g_signal_connect(s_size, "activate", G_CALLBACK(on_sort_mode_selected), GINT_TO_POINTER(SORT_SIZE));
        g_signal_connect(edit_widgets, "activate", G_CALLBACK(on_edit_widgets), widget);

        if (current_mode == MODE_NORMAL) gtk_label_set_markup(GTK_LABEL(gtk_bin_get_child(GTK_BIN(m_normal))), "<b>Normal (Desktop)</b>");
        if (current_mode == MODE_WORK) gtk_label_set_markup(GTK_LABEL(gtk_bin_get_child(GTK_BIN(m_work))), "<b>Work (Work)</b>");
        if (current_mode == MODE_WIDGETS) gtk_label_set_markup(GTK_LABEL(gtk_bin_get_child(GTK_BIN(m_widgets))), "<b>Widgets Only</b>");
        sort_mode_to_markup(SORT_MANUAL, s_manual);
        sort_mode_to_markup(SORT_NAME, s_name);
        sort_mode_to_markup(SORT_TYPE, s_type);
        sort_mode_to_markup(SORT_DATE_MODIFIED, s_date);
        sort_mode_to_markup(SORT_SIZE, s_size);

        gtk_menu_shell_append(GTK_MENU_SHELL(mode_menu), m_normal);
        gtk_menu_shell_append(GTK_MENU_SHELL(mode_menu), m_work);
        gtk_menu_shell_append(GTK_MENU_SHELL(mode_menu), m_widgets);

        gtk_menu_shell_append(GTK_MENU_SHELL(sort_menu), s_manual);
        gtk_menu_shell_append(GTK_MENU_SHELL(sort_menu), s_name);
        gtk_menu_shell_append(GTK_MENU_SHELL(sort_menu), s_type);
        gtk_menu_shell_append(GTK_MENU_SHELL(sort_menu), s_date);
        gtk_menu_shell_append(GTK_MENU_SHELL(sort_menu), s_size);

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), new_folder);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), create_doc);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), term);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), paste);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mode_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sort_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), edit_widgets);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), wallpaper);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }

    return FALSE;
}

gboolean on_bg_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    (void)widget;
    (void)data;

    if (is_selecting) {
        GList *children;
        current_x = event->x;
        current_y = event->y;
        gtk_widget_queue_draw(icon_layout);

        {
            double x = MIN(start_x, current_x);
            double y = MIN(start_y, current_y);
            double w = fabs(current_x - start_x);
            double h = fabs(current_y - start_y);

            children = gtk_container_get_children(GTK_CONTAINER(icon_layout));
            for (GList *l = children; l != NULL; l = l->next) {
                GtkWidget *item = GTK_WIDGET(l->data);
                GtkAllocation alloc;
                gtk_widget_get_allocation(item, &alloc);

                if (alloc.x < x + w && alloc.x + alloc.width > x &&
                    alloc.y < y + h && alloc.y + alloc.height > y) {
                    select_item(item);
                } else {
                    deselect_item(item);
                }
            }
            g_list_free(children);
        }
    }

    return FALSE;
}

gboolean on_bg_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    (void)data;

    if (event->button == 1 && is_selecting) {
        is_selecting = FALSE;
        gtk_widget_queue_draw(icon_layout);
    }

    return FALSE;
}
