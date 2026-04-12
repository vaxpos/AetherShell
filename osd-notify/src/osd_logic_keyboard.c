#include "osd_logic_keyboard.h"
#include "osd_logic_state.h"
#include <glib.h>
#include <ctype.h>
#include <string.h>
#include <xkbcommon/xkbregistry.h>

static struct rxkb_context *rxkb_ctx = NULL;
static char loaded_layouts[8][16];
static int loaded_layouts_count = 0;
static int current_layout_index = -1;

static void to_upper_str(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = toupper((unsigned char)str[i]);
    }
}

static void normalize_layout_name(char *layout) {
    if (!layout || !layout[0]) return;

    to_upper_str(layout);
    if (strcmp(layout, "ARA") == 0 || strcmp(layout, "IQ") == 0) {
        strcpy(layout, "AR");
    } else if (strcmp(layout, "USA") == 0) {
        strcpy(layout, "US");
    }
}

static const char *brief_layout_name(const char *layout_name) {
    if (!layout_name || !layout_name[0]) return NULL;

    if (!rxkb_ctx) {
        rxkb_ctx = rxkb_context_new(RXKB_CONTEXT_NO_FLAGS);
        if (rxkb_ctx) {
            rxkb_context_parse_default_ruleset(rxkb_ctx);
        }
    }

    if (!rxkb_ctx) return NULL;

    struct rxkb_layout *layout = rxkb_layout_first(rxkb_ctx);
    for (; layout != NULL; layout = rxkb_layout_next(layout)) {
        const char *name = rxkb_layout_get_name(layout);
        const char *description = rxkb_layout_get_description(layout);
        if ((name && g_strcmp0(name, layout_name) == 0) ||
            (description && g_strcmp0(description, layout_name) == 0)) {
            return rxkb_layout_get_brief(layout);
        }
    }

    return NULL;
}

static const char *display_layout_name(const char *layout_name) {
    if (!layout_name || !layout_name[0]) return NULL;

    if (g_ascii_strcasecmp(layout_name, "AR") == 0 ||
        g_ascii_strcasecmp(layout_name, "ARA") == 0 ||
        g_ascii_strcasecmp(layout_name, "IQ") == 0 ||
        g_ascii_strcasecmp(layout_name, "Arabic") == 0) {
        return "Arabic";
    }

    if (g_ascii_strcasecmp(layout_name, "US") == 0 ||
        g_ascii_strcasecmp(layout_name, "USA") == 0 ||
        g_ascii_strcasecmp(layout_name, "EN") == 0 ||
        g_ascii_strcasecmp(layout_name, "English") == 0) {
        return "English";
    }

    return layout_name;
}

void osd_logic_keyboard_update_layouts_from_json_array(JsonArray *layouts) {
    if (!layouts) return;

    memset(loaded_layouts, 0, sizeof(loaded_layouts));
    loaded_layouts_count = 0;

    guint n_layouts = json_array_get_length(layouts);
    for (guint i = 0; i < n_layouts && loaded_layouts_count < 8; ++i) {
        const char *layout_name = json_array_get_string_element(layouts, i);
        if (!layout_name || !layout_name[0]) continue;

        const char *brief = brief_layout_name(layout_name);
        const char *display_name = display_layout_name(brief && brief[0] ? brief : layout_name);
        if (display_name && display_name[0]) {
            g_strlcpy(loaded_layouts[loaded_layouts_count], display_name, sizeof(loaded_layouts[0]));
        } else {
            g_strlcpy(loaded_layouts[loaded_layouts_count], layout_name, sizeof(loaded_layouts[0]));
        }

        normalize_layout_name(loaded_layouts[loaded_layouts_count]);
        loaded_layouts_count++;
    }
}

gboolean osd_logic_keyboard_update_layouts_from_delimited_string(const char *layouts, char separator) {
    if (!layouts || !layouts[0]) return FALSE;

    memset(loaded_layouts, 0, sizeof(loaded_layouts));
    loaded_layouts_count = 0;

    const char *cursor = layouts;
    while (*cursor && loaded_layouts_count < 8) {
        const char *end = strchr(cursor, separator);
        gsize len = end ? (gsize)(end - cursor) : strlen(cursor);

        if (len > 0) {
            gchar *layout_name = g_strndup(cursor, len);
            g_strstrip(layout_name);

            if (layout_name[0]) {
                const char *brief = brief_layout_name(layout_name);
                const char *display_name = display_layout_name(brief && brief[0] ? brief : layout_name);
                if (display_name && display_name[0]) {
                    g_strlcpy(loaded_layouts[loaded_layouts_count], display_name, sizeof(loaded_layouts[0]));
                } else {
                    g_strlcpy(loaded_layouts[loaded_layouts_count], layout_name, sizeof(loaded_layouts[0]));
                }

                normalize_layout_name(loaded_layouts[loaded_layouts_count]);
                loaded_layouts_count++;
            }

            g_free(layout_name);
        }

        if (!end) break;
        cursor = end + 1;
    }

    return loaded_layouts_count > 0;
}

gboolean osd_logic_keyboard_apply_layout_state(int current, gboolean show_popup) {
    if (loaded_layouts_count <= 0) return FALSE;
    if (current < 0 || current >= loaded_layouts_count) return FALSE;

    const char *current_text = osd_logic_state_get_text();
    if (current == current_layout_index && g_strcmp0(current_text, loaded_layouts[current]) == 0) {
        return TRUE;
    }

    current_layout_index = current;
    osd_logic_state_set_text(loaded_layouts[current_layout_index]);
    osd_logic_state_set_type(OSD_KEYBOARD);

    if (show_popup) {
        osd_logic_state_show_osd();
    }

    return TRUE;
}

void osd_logic_keyboard_set_label(const char *label, gboolean show_popup) {
    if (!label || !label[0]) return;
    const char *current_text = osd_logic_state_get_text();
    if (g_strcmp0(current_text, label) == 0 && osd_logic_state_get_type() == OSD_KEYBOARD) {
        return;
    }

    osd_logic_state_set_text(label);
    current_layout_index = -1;
    osd_logic_state_set_type(OSD_KEYBOARD);

    if (show_popup) {
        osd_logic_state_show_osd();
    }
}

OsdProtocolCallbacks osd_logic_keyboard_build_protocol_callbacks(
    gboolean (*is_wayland_session)(void)) {
    OsdProtocolCallbacks callbacks = {
        .apply_keyboard_layout_state = osd_logic_keyboard_apply_layout_state,
        .update_layouts_from_json_array = osd_logic_keyboard_update_layouts_from_json_array,
        .update_layouts_from_delimited_string = osd_logic_keyboard_update_layouts_from_delimited_string,
        .is_wayland_session = is_wayland_session,
        .set_keyboard_label = osd_logic_keyboard_set_label,
    };
    return callbacks;
}
