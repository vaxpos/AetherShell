#ifndef VENOM_GUI_OSD_PROTOCOLS_H
#define VENOM_GUI_OSD_PROTOCOLS_H

#include <glib.h>
#include <json-glib/json-glib.h>

typedef struct {
    gboolean (*apply_keyboard_layout_state)(int current, gboolean show_popup);
    void (*update_layouts_from_json_array)(JsonArray *layouts);
    gboolean (*update_layouts_from_delimited_string)(const char *layouts, char separator);
    gboolean (*is_wayland_session)(void);
    void (*set_keyboard_label)(const char *label, gboolean show_popup);
} OsdProtocolCallbacks;

void osd_protocols_init(const OsdProtocolCallbacks *callbacks);
void osd_protocols_shutdown(void);

#endif
