#ifndef PULSE_VOLUME_H
#define PULSE_VOLUME_H

#include <glib.h>
#include <stdint.h>

/* ── Master sink (system volume) ─────────────────────────────────────────── */

typedef void (*VolumeChangedCallback)(int percent, gboolean muted, gpointer user_data);

void pulse_volume_init(VolumeChangedCallback cb, gpointer user_data);
void pulse_volume_set(int percent);
int  pulse_volume_get_current(void);   /* returns cached %, or -1 */
gboolean pulse_volume_is_muted(void);  /* returns cached mute state */

typedef struct {
    gchar *sink_name;
    gchar *port_name;
    gchar *description;
    gboolean is_active;
} AudioSinkInfo;

typedef void (*SinksFetchedCallback)(GList *devices, gpointer user_data);
void pulse_sinks_get(SinksFetchedCallback cb, gpointer user_data);
void pulse_device_set(const char *sink_name, const char *port_name);
const char *pulse_get_default_sink_name(void);
void pulse_sinks_free(GList *list);

/* ── Per-application sink-input (mixer) ──────────────────────────────────── */

typedef struct {
    uint32_t  index;
    gchar    *app_name;
    int       volume_percent;   /* 0-150 */
    gboolean  muted;
} SinkInputInfo;

/* cb is called (on the GLib main loop) whenever the sink-input list changes */
typedef void (*SinkInputsChangedCallback)(GList *sink_inputs, gpointer user_data);

void pulse_sink_inputs_init(SinkInputsChangedCallback cb, gpointer user_data);
void pulse_sink_input_set_volume(uint32_t index, int percent);
void pulse_sink_input_set_mute(uint32_t index, gboolean mute);

/* Free the list returned via the callback */
void pulse_sink_inputs_free(GList *list);

/* ── Master source (microphone) ─────────────────────────────────────────── */

typedef void (*MicVolumeChangedCallback)(int percent, gboolean muted, gpointer user_data);

void pulse_mic_init(MicVolumeChangedCallback cb, gpointer user_data);
void pulse_mic_set(int percent);
int  pulse_mic_get_current(void);   /* returns cached %, or -1 */
gboolean pulse_mic_is_muted(void);  /* returns cached mute state */

/* ── Per-application source-output (mic mixer) ──────────────────────────────────── */

typedef struct {
    uint32_t  index;
    gchar    *app_name;
    int       volume_percent;   /* 0-150 */
    gboolean  muted;
} SourceOutputInfo;

/* cb is called (on the GLib main loop) whenever the source-output list changes */
typedef void (*SourceOutputsChangedCallback)(GList *source_outputs, gpointer user_data);

void pulse_source_outputs_init(SourceOutputsChangedCallback cb, gpointer user_data);
void pulse_source_output_set_volume(uint32_t index, int percent);
void pulse_source_output_set_mute(uint32_t index, gboolean mute);

/* Free the list returned via the callback */
void pulse_source_outputs_free(GList *list);

#endif // PULSE_VOLUME_H
