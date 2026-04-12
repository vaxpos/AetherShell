#include "osd_logic.h"
#include "osd_logic_audio.h"
#include "osd_logic_brightness.h"
#include "osd_logic_draw.h"
#include "osd_logic_keyboard.h"
#include "osd_logic_state.h"

void osd_logic_init(OsdShowCallback show_cb) {
    osd_logic_state_init(show_cb);
}

void osd_logic_setup_pulseaudio(void) {
    osd_logic_audio_setup_pulseaudio();
}

void osd_logic_setup_brightness_monitoring(void) {
    osd_logic_brightness_setup_monitoring();
}

OsdProtocolCallbacks osd_logic_build_protocol_callbacks(gboolean (*is_wayland_session)(void)) {
    return osd_logic_keyboard_build_protocol_callbacks(is_wayland_session);
}

gboolean osd_logic_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    return osd_logic_draw(widget, cr, data);
}
