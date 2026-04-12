#ifndef VENOM_GUI_OSD_LOGIC_STATE_H
#define VENOM_GUI_OSD_LOGIC_STATE_H

#include <gtk/gtk.h>

typedef void (*OsdShowCallback)(int width, int height, guint duration_ms);

typedef enum {
    OSD_KEYBOARD,
    OSD_VOLUME,
    OSD_BRIGHTNESS,
    OSD_MIC
} OsdType;

void osd_logic_state_init(OsdShowCallback show_cb);
void osd_logic_state_show_osd(void);

void osd_logic_state_set_type(OsdType type);
OsdType osd_logic_state_get_type(void);

void osd_logic_state_set_text(const char *text);
const char *osd_logic_state_get_text(void);

void osd_logic_state_set_volume(int volume);
int osd_logic_state_get_volume(void);

void osd_logic_state_set_muted(int muted);
int osd_logic_state_get_muted(void);

void osd_logic_state_set_max_volume(int max_volume);
int osd_logic_state_get_max_volume(void);

void osd_logic_state_set_brightness(int brightness);
int osd_logic_state_get_brightness(void);

void osd_logic_state_set_mic_muted(int muted);
int osd_logic_state_get_mic_muted(void);

void osd_logic_state_set_mic_text(const char *text);
const char *osd_logic_state_get_mic_text(void);

#endif
