#include "drives.h"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>
#include "utils.h"

GtkWidget *drives_box = NULL;      /* Container for drive buttons */
static GVolumeMonitor *volume_monitor = NULL;

/* Forward declaration */
static void refresh_drives(void);

/* Open the mount point in the file manager */
static void on_drive_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    GMount *mount = G_MOUNT(data);

    GFile *root = g_mount_get_root(mount);
    if (!root) return;

    gchar *path = g_file_get_uri(root);
    g_object_unref(root);

    if (!path) return;

    open_with_preferred_file_manager(path);
    g_free(path);
}

/* Eject/unmount drive from context menu */
static void on_eject_clicked(GtkMenuItem *item, gpointer data) {
    (void)item;
    GMount *mount = G_MOUNT(data);

    if (g_mount_can_eject(mount)) {
        g_mount_eject_with_operation(mount, G_MOUNT_UNMOUNT_NONE, NULL, NULL, NULL, NULL);
    } else if (g_mount_can_unmount(mount)) {
        g_mount_unmount_with_operation(mount, G_MOUNT_UNMOUNT_NONE, NULL, NULL, NULL, NULL);
    }
}

/* right-click context menu (eject) */
static gboolean on_drive_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GMount *mount = G_MOUNT(data);
        GtkWidget *menu = gtk_menu_new();

        GtkWidget *eject_item = gtk_menu_item_new_with_label("Unmount");
        g_signal_connect(eject_item, "activate", G_CALLBACK(on_eject_clicked), mount);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), eject_item);
        gtk_widget_show_all(menu);

        gtk_menu_popup_at_widget(GTK_MENU(menu), widget,
                                 GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_SOUTH_WEST,
                                 (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

/* Rebuild all drive buttons */
static void refresh_drives(void) {
    if (!drives_box) return;

    /* Remove all existing drive buttons */
    GList *children = gtk_container_get_children(GTK_CONTAINER(drives_box));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    /* Get current mounts */
    GList *mounts = g_volume_monitor_get_mounts(volume_monitor);

    for (GList *l = mounts; l != NULL; l = l->next) {
        GMount *mount = G_MOUNT(l->data);

        /* Skip the root filesystem and others that are not "interesting" */
        GVolume *volume = g_mount_get_volume(mount);
        if (volume) {
            /* Only show mounts that have removable / external drive backing */
            GDrive *drive = g_volume_get_drive(volume);
            if (drive) {
                gboolean removable = g_drive_is_removable(drive);
                g_object_unref(drive);
                if (!removable) {
                    g_object_unref(volume);
                    g_object_unref(mount);
                    continue;
                }
            }
            g_object_unref(volume);
        } else {
            /* No backing volume – likely a system pseudo-mount, skip */
            g_object_unref(mount);
            continue;
        }

        /* Build a button for this mount */
        GtkWidget *btn = gtk_button_new();
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        gtk_widget_set_name(btn, "drive-button");

        /* Tooltip = mount name */
        gchar *name = g_mount_get_name(mount);
        gtk_widget_set_tooltip_text(btn, name ? name : "قرص");
        g_free(name);

        /* Icon from system via GIcon */
        GIcon *gicon = g_mount_get_icon(mount);
        GtkWidget *image = gtk_image_new_from_gicon(gicon, GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_image_set_pixel_size(GTK_IMAGE(image), 32);
        g_object_unref(gicon);

        gtk_container_add(GTK_CONTAINER(btn), image);

        /* Keep a ref to mount for the click callback; release when button destroyed */
        g_object_ref(mount);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_drive_clicked), mount);
        g_signal_connect(btn, "button-press-event", G_CALLBACK(on_drive_button_press), mount);
        g_signal_connect_swapped(btn, "destroy", G_CALLBACK(g_object_unref), mount);

        /* Pack start – drives appear left-to-right inside the drives_box;
           the drives_box itself is packed with pack_end into the main dock box */
        gtk_box_pack_start(GTK_BOX(drives_box), btn, FALSE, FALSE, 0);
        gtk_widget_show_all(btn);

        g_object_unref(mount);
    }
    g_list_free(mounts);
}

/* Volume monitor signal callbacks */
static void on_mount_added(GVolumeMonitor *monitor, GMount *mount, gpointer data) {
    (void)monitor; (void)mount; (void)data;
    refresh_drives();
}

static void on_mount_removed(GVolumeMonitor *monitor, GMount *mount, gpointer data) {
    (void)monitor; (void)mount; (void)data;
    refresh_drives();
}

void create_drives_area(GtkWidget *box) {
    /* Get the singleton volume monitor */
    volume_monitor = g_volume_monitor_get();

    /* Create a horizontal box to hold drive buttons */
    drives_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_name(drives_box, "drives-box");

    /* Pack the drives area to the right (next to trash, left of trash) */
    gtk_box_pack_end(GTK_BOX(box), drives_box, FALSE, FALSE, 0);
    gtk_widget_show(drives_box);

    /* Populate initially */
    refresh_drives();

    /* Watch for future mount changes */
    g_signal_connect(volume_monitor, "mount-added",   G_CALLBACK(on_mount_added),   NULL);
    g_signal_connect(volume_monitor, "mount-removed", G_CALLBACK(on_mount_removed), NULL);
}
