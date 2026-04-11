#include <gtk/gtk.h>
void test_hierarchy(GtkWidget *w) {
    GtkWidget *p = w;
    while(p) {
        g_print("WIDGET: %s\n", G_OBJECT_TYPE_NAME(p));
        p = gtk_widget_get_parent(p);
    }
}
