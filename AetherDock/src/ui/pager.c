#include "pager.h"

static GtkWidget *pager_drawing_area = NULL;
static PagerClickCallback click_callback = NULL;
static gpointer click_callback_data = NULL;

void pager_init(void) {
}

static gboolean on_pager_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)widget;
    (void)data;

    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.35);
    cairo_paint(cr);

    cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 0.2);
    cairo_rectangle(cr, 1, 1,
                    gtk_widget_get_allocated_width(widget) - 2,
                    gtk_widget_get_allocated_height(widget) - 2);
    cairo_stroke(cr);

    return FALSE;
}

static gboolean on_pager_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    (void)event;
    (void)data;

    if (click_callback) {
        click_callback(0, click_callback_data);
    }

    return TRUE;
}

static void on_pager_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    pager_drawing_area = NULL;
}

GtkWidget *pager_create_widget(void) {
    if (pager_drawing_area) {
        return pager_drawing_area;
    }

    pager_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(pager_drawing_area, 240, 160);
    g_signal_connect(pager_drawing_area, "draw", G_CALLBACK(on_pager_draw), NULL);
    g_signal_connect(pager_drawing_area, "destroy", G_CALLBACK(on_pager_destroy), NULL);

    gtk_widget_add_events(pager_drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(pager_drawing_area, "button-press-event", G_CALLBACK(on_pager_click), NULL);

    return pager_drawing_area;
}

void pager_update(void) {
    if (pager_drawing_area) {
        gtk_widget_queue_draw(pager_drawing_area);
    }
}

void pager_set_click_callback(PagerClickCallback callback, gpointer user_data) {
    click_callback = callback;
    click_callback_data = user_data;
}
