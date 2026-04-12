#include "osd_logic_state.h"
#include <glib.h>
#include <string.h>

#define SHOW_DURATION_MS 1500

static OsdShowCallback show_callback = NULL;
static OsdType current_osd_type = OSD_KEYBOARD;
static char current_text[32] = "";
static int current_volume = -1;
static int is_muted = -1;
static int current_brightness = 0;
static int mic_is_muted = -1;
static char current_mic_text[16] = "";
static int current_max_volume = 100;

void osd_logic_state_init(OsdShowCallback show_cb) {
    show_callback = show_cb;
}

void osd_logic_state_show_osd(void) {
    if (!show_callback) return;

    int width = 200;
    int height = 200;
    if (current_osd_type == OSD_KEYBOARD) {
        height = 100;
    } else {
        height = 200;
    }

    show_callback(width, height, SHOW_DURATION_MS);
}

void osd_logic_state_set_type(OsdType type) {
    current_osd_type = type;
}

OsdType osd_logic_state_get_type(void) {
    return current_osd_type;
}

void osd_logic_state_set_text(const char *text) {
    if (!text) text = "";
    g_strlcpy(current_text, text, sizeof(current_text));
}

const char *osd_logic_state_get_text(void) {
    return current_text;
}

void osd_logic_state_set_volume(int volume) {
    current_volume = volume;
}

int osd_logic_state_get_volume(void) {
    return current_volume;
}

void osd_logic_state_set_muted(int muted) {
    is_muted = muted;
}

int osd_logic_state_get_muted(void) {
    return is_muted;
}

void osd_logic_state_set_max_volume(int max_volume) {
    current_max_volume = max_volume;
}

int osd_logic_state_get_max_volume(void) {
    return current_max_volume;
}

void osd_logic_state_set_brightness(int brightness) {
    current_brightness = brightness;
}

int osd_logic_state_get_brightness(void) {
    return current_brightness;
}

void osd_logic_state_set_mic_muted(int muted) {
    mic_is_muted = muted;
}

int osd_logic_state_get_mic_muted(void) {
    return mic_is_muted;
}

void osd_logic_state_set_mic_text(const char *text) {
    if (!text) text = "";
    g_strlcpy(current_mic_text, text, sizeof(current_mic_text));
}

const char *osd_logic_state_get_mic_text(void) {
    return current_mic_text;
}
