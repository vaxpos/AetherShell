#include "trash.h"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

GtkWidget *trash_button = NULL;
static GtkWidget *trash_image  = NULL;

/* Check if trash is non-empty */
static gboolean trash_is_full(void) {
    const gchar *home = g_get_home_dir();
    gchar *trash_path = g_build_filename(home, ".local", "share", "Trash", "files", NULL);

    GDir *dir = g_dir_open(trash_path, 0, NULL);
    gboolean has_files = FALSE;

    if (dir) {
        if (g_dir_read_name(dir) != NULL)
            has_files = TRUE;
        g_dir_close(dir);
    }

    g_free(trash_path);
    return has_files;
}

/* Update icon based on trash state */
void trash_update_icon(void) {
    if (!trash_image) return;

    const gchar *icon_name = trash_is_full() ? "user-trash-full" : "user-trash";
    gtk_image_set_from_icon_name(GTK_IMAGE(trash_image), icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_image_set_pixel_size(GTK_IMAGE(trash_image), 32);
}

/* Periodic update callback */
static gboolean on_trash_timeout(gpointer data) {
    (void)data;
    trash_update_icon();
    return G_SOURCE_CONTINUE;
}

/* Empty trash action */
static void on_empty_trash_clicked(GtkMenuItem *item, gpointer data) {
    (void)item; (void)data;
    
    GError *error = NULL;
    /* Use GIO to empty trash (standard way on modern Linux) */
    gchar *argv[] = { "gio", "trash", "--empty", NULL };
    if (!g_spawn_async(NULL, argv, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                       NULL, NULL, NULL, &error)) {
        g_warning("Failed to empty trash: %s", error->message);
        g_error_free(error);
    }
    
    /* Update icon immediately after emptying */
    trash_update_icon();
}

/* Right click handler for trash button */
static gboolean on_trash_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)data;
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkWidget *menu = gtk_menu_new();
        
        GtkWidget *empty_item = gtk_menu_item_new_with_label("Empty Trash");
        g_signal_connect(empty_item, "activate", G_CALLBACK(on_empty_trash_clicked), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), empty_item);
        
        /* Only enable if trash is actually full */
        gtk_widget_set_sensitive(empty_item, trash_is_full());
        
        gtk_widget_show_all(menu);
        
        gtk_menu_popup_at_widget(GTK_MENU(menu), widget,
                                 GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_SOUTH_WEST,
                                 (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

/* Open trash in file manager */
static void on_trash_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;

    open_with_preferred_file_manager("trash:///");
}

void create_trash_button(GtkWidget *box) {
    trash_button = gtk_button_new();
    gtk_widget_set_name(trash_button, "trash-button");
    gtk_button_set_relief(GTK_BUTTON(trash_button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(trash_button, "Trash");

    /* Icon */
    const gchar *icon_name = trash_is_full() ? "user-trash-full" : "user-trash";
    trash_image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_image_set_pixel_size(GTK_IMAGE(trash_image), 32);
    gtk_container_add(GTK_CONTAINER(trash_button), trash_image);

    /* Hook up right-click for context menu */
    g_signal_connect(trash_button, "button-press-event", G_CALLBACK(on_trash_button_press), NULL);

    g_signal_connect(trash_button, "clicked", G_CALLBACK(on_trash_clicked), NULL);

    /* Pack to the right (pack_end) - will appear to the left of the launcher */
    gtk_box_pack_end(GTK_BOX(box), trash_button, FALSE, FALSE, 0);
    gtk_widget_show_all(trash_button);

    /* Periodically check trash state every 3 seconds */
    g_timeout_add_seconds(3, on_trash_timeout, NULL);
}
