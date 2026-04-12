#ifndef VENOM_GUI_OSD_LOGIC_H
#define VENOM_GUI_OSD_LOGIC_H

#include <gtk/gtk.h>
#include "osd_logic_state.h"
#include "osd_protocols.h"

void osd_logic_init(OsdShowCallback show_cb);
void osd_logic_setup_pulseaudio(void);
void osd_logic_setup_brightness_monitoring(void);
OsdProtocolCallbacks osd_logic_build_protocol_callbacks(gboolean (*is_wayland_session)(void));
gboolean osd_logic_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data);

#endif
