#include "app_icon.h"
#include "../core/icon_loader.h"
#include <glib/gspawn.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Widget struct
 * ------------------------------------------------------------------------- */

struct _VenomAppIcon {
    GtkButton  parent_instance;
    AppEntry  *entry;
    GtkWidget *image;
    GtkWidget *label;
};

G_DEFINE_TYPE (VenomAppIcon, venom_app_icon, GTK_TYPE_BUTTON)

/* -------------------------------------------------------------------------
 * Icon ready callback (async)
 * ------------------------------------------------------------------------- */

typedef struct {
    VenomAppIcon *icon;
    gulong        destroy_id;
} IconLoadCtx;

static void
on_icon_widget_destroy (GtkWidget *w, gpointer data)
{
    (void) w;
    IconLoadCtx *ctx = data;
    /* Mark widget as gone so the callback won't touch it */
    ctx->icon = NULL;
}

static void
on_icon_ready (GdkPixbuf *pixbuf, gpointer user_data)
{
    IconLoadCtx  *ctx  = user_data;
    VenomAppIcon *self = ctx->icon;

    if (self && pixbuf) {
        gtk_image_set_from_pixbuf (GTK_IMAGE (self->image), pixbuf);
    }

    if (self)
        g_signal_handler_disconnect (GTK_WIDGET (self), ctx->destroy_id);

    g_free (ctx);
}

/* -------------------------------------------------------------------------
 * Launch the application
 * ------------------------------------------------------------------------- */

static void
launch_app (VenomAppIcon *self)
{
    if (!self->entry || !self->entry->exec) return;

    GError *err = NULL;
    if (!g_spawn_command_line_async (self->entry->exec, &err)) {
        g_warning ("Failed to launch '%s': %s",
                   self->entry->name, err->message);
        g_error_free (err);
        return;
    }

    /* Close the launcher window */
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));
    if (GTK_IS_WINDOW (toplevel))
        gtk_widget_hide (toplevel);
}

static void
on_clicked (GtkButton *btn, gpointer user_data)
{
    (void) user_data;
    launch_app (VENOM_APP_ICON (btn));
}

/* -------------------------------------------------------------------------
 * Context Menu Actions
 * ------------------------------------------------------------------------- */

static void
on_menu_run (GtkMenuItem *item, gpointer data)
{
    (void) item;
    launch_app (VENOM_APP_ICON (data));
}

static void
on_menu_shortcut (GtkMenuItem *item, gpointer data)
{
    (void) item;
    VenomAppIcon *self  = VENOM_APP_ICON (data);
    AppEntry     *entry = self->entry;

    if (!entry || !entry->desktop_path) return;

    /* Copy .desktop to ~/Desktop and make executable */
    const char *home    = g_get_home_dir ();
    char *desktop_dir   = g_build_filename (home, "Desktop", NULL);
    char *filename      = g_path_get_basename (entry->desktop_path);
    char *dest_path     = g_build_filename (desktop_dir, filename, NULL);

    /* Hide window so user sees the change */
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));
    if (GTK_IS_WINDOW (toplevel)) gtk_widget_hide (toplevel);

    /* Use sh -c to execute a compound shell command */
    char *cmd = g_strdup_printf ("sh -c \"cp '%s' '%s' && chmod +x '%s'\"",
                                 entry->desktop_path, dest_path, dest_path);

    g_spawn_command_line_async (cmd, NULL);

    g_free (cmd);
    g_free (dest_path);
    g_free (filename);
    g_free (desktop_dir);
}

static void
on_menu_uninstall (GtkMenuItem *item, gpointer data)
{
    (void) item;
    VenomAppIcon *self  = VENOM_APP_ICON (data);
    AppEntry     *entry = self->entry;

    if (!entry || !entry->desktop_path) return;

    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));
    if (GTK_IS_WINDOW (toplevel)) gtk_widget_hide (toplevel);

    /* Use pkexec to prompt password and delete the .desktop file */
    char *cmd = g_strdup_printf ("pkexec sh -c \"rm -f '%s'\"", entry->desktop_path);
    g_spawn_command_line_async (cmd, NULL);
    g_free (cmd);
}

/* -------------------------------------------------------------------------
 * Right Click Menu Setup
 * ------------------------------------------------------------------------- */

static gboolean
on_button_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void) data;
    VenomAppIcon *self = VENOM_APP_ICON (widget);

    /* Right click (button 3) reveals context menu */
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkWidget *menu = gtk_menu_new ();

        GtkWidget *item_run = gtk_menu_item_new_with_label ("Run");
        g_signal_connect (item_run, "activate", G_CALLBACK (on_menu_run), self);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_run);

        GtkWidget *item_shortcut = gtk_menu_item_new_with_label ("Create Shortcut");
        g_signal_connect (item_shortcut, "activate", G_CALLBACK (on_menu_shortcut), self);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_shortcut);

        GtkWidget *item_uninstall = gtk_menu_item_new_with_label ("Uninstall");
        g_signal_connect (item_uninstall, "activate", G_CALLBACK (on_menu_uninstall), self);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_uninstall);

        gtk_widget_show_all (menu);
        gtk_menu_popup_at_pointer (GTK_MENU (menu), (GdkEvent *) event);

        return TRUE; /* Handled */
    }

    return FALSE;
}

/* -------------------------------------------------------------------------
 * GObject class init
 * ------------------------------------------------------------------------- */

static void
venom_app_icon_class_init (VenomAppIconClass *klass)
{
    (void) klass;
}

static void
venom_app_icon_init (VenomAppIcon *self)
{
    gtk_style_context_add_class (
        gtk_widget_get_style_context (GTK_WIDGET (self)),
        "app-icon-button");

    gtk_button_set_relief (GTK_BUTTON (self), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus (GTK_WIDGET (self), FALSE);

    /* Vertical box inside button */
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
    gtk_container_add (GTK_CONTAINER (self), box);

    /* Image placeholder */
    self->image = gtk_image_new_from_icon_name ("application-x-executable",
                                                GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size (GTK_IMAGE (self->image), ICON_LOAD_SIZE);
    gtk_widget_set_halign (self->image, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (box), self->image, FALSE, FALSE, 0);

    /* Label */
    self->label = gtk_label_new ("");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (self->label), "app-name");
    gtk_label_set_max_width_chars (GTK_LABEL (self->label), 12);
    gtk_label_set_ellipsize (GTK_LABEL (self->label), PANGO_ELLIPSIZE_END);
    gtk_label_set_lines (GTK_LABEL (self->label), 2);
    gtk_label_set_line_wrap (GTK_LABEL (self->label), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (self->label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_justify (GTK_LABEL (self->label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign (self->label, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (box), self->label, FALSE, FALSE, 0);

    /* Standard left click */
    g_signal_connect (self, "clicked", G_CALLBACK (on_clicked), NULL);

    /* Manual button-press for right click */
    g_signal_connect (self, "button-press-event", G_CALLBACK (on_button_press), NULL);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

GtkWidget *
venom_app_icon_new (AppEntry *entry)
{
    VenomAppIcon *self = g_object_new (VENOM_TYPE_APP_ICON, NULL);
    self->entry = entry;

    gtk_label_set_text (GTK_LABEL (self->label),
                        entry->name ? entry->name : "");

    /* Async icon load */
    if (entry->icon_name) {
        IconLoader  *loader = icon_loader_get ();
        IconLoadCtx *ctx    = g_new0 (IconLoadCtx, 1);
        ctx->icon = self;
        ctx->destroy_id = g_signal_connect (
            GTK_WIDGET (self), "destroy",
            G_CALLBACK (on_icon_widget_destroy), ctx);

        icon_loader_load_async (loader, entry->icon_name,
                                on_icon_ready, ctx);
    }

    return GTK_WIDGET (self);
}

AppEntry *
venom_app_icon_get_entry (VenomAppIcon *icon)
{
    g_return_val_if_fail (VENOM_IS_APP_ICON (icon), NULL);
    return icon->entry;
}
