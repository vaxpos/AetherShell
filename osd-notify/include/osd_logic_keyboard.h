#ifndef VENOM_GUI_OSD_LOGIC_KEYBOARD_H
#define VENOM_GUI_OSD_LOGIC_KEYBOARD_H

#include <json-glib/json-glib.h>
#include "osd_protocols.h"

void osd_logic_keyboard_update_layouts_from_json_array(JsonArray *layouts);
gboolean osd_logic_keyboard_update_layouts_from_delimited_string(const char *layouts, char separator);
gboolean osd_logic_keyboard_apply_layout_state(int current, gboolean show_popup);
void osd_logic_keyboard_set_label(const char *label, gboolean show_popup);

OsdProtocolCallbacks osd_logic_keyboard_build_protocol_callbacks(
    gboolean (*is_wayland_session)(void));

#endif
