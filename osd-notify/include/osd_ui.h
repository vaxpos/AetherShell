#ifndef VENOM_GUI_OSD_UI_H
#define VENOM_GUI_OSD_UI_H

#include <gtk/gtk.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *drawing_area;
    gboolean use_layer_shell;
    guint hide_timer_id;
} OsdUi;

void osd_ui_init(OsdUi *ui, GCallback draw_cb, gpointer draw_data);
void osd_ui_show(OsdUi *ui, int width, int height, guint duration_ms);
void osd_ui_queue_draw(OsdUi *ui);
void osd_ui_update_position(OsdUi *ui, int width, int height);
void osd_ui_apply_input_passthrough(OsdUi *ui);

#endif
