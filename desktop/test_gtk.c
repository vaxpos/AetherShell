#include <gtk/gtk.h>
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    GtkWidget *layout = gtk_layout_new(NULL, NULL);
    GtkWidget *slot = gtk_event_box_new();
    gtk_layout_put(GTK_LAYOUT(layout), slot, 0, 0);
    g_print("Layout ptr: %p\n", layout);
    g_print("Slot parent ptr: %p\n", gtk_widget_get_parent(slot));
    g_print("Slot parent name: %s\n", G_OBJECT_TYPE_NAME(gtk_widget_get_parent(slot)));
    return 0;
}
