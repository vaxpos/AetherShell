#include "osd_logic_audio.h"
#include "osd_logic_state.h"
#include <glib.h>
#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <string.h>

static pa_glib_mainloop *my_pa_mainloop = NULL;
static pa_context *pa_ctx = NULL;
static char default_source_name[256] = "";

static gboolean get_overamplification(void) {
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!conn) {
        if (error) g_error_free(error);
        return FALSE;
    }

    GVariant *result = g_dbus_connection_call_sync(
        conn,
        "org.venom.Audio",
        "/org/venom/Audio",
        "org.venom.Audio",
        "GetOveramplification",
        NULL,
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        NULL,
        &error
    );

    gboolean is_over = FALSE;
    if (result) {
        g_variant_get(result, "(b)", &is_over);
        g_variant_unref(result);
    } else if (error) {
        g_error_free(error);
    }
    g_object_unref(conn);
    return is_over;
}

static void refresh_mic_osd(int muted) {
    if (osd_logic_state_get_mic_muted() == muted) return;

    osd_logic_state_set_mic_muted(muted);
    osd_logic_state_set_mic_text(muted ? "Muted" : "Live");
    osd_logic_state_set_type(OSD_MIC);
    printf("[PA] Microphone state updated: %s\n", osd_logic_state_get_mic_text());
    osd_logic_state_show_osd();
}

static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void)c;
    (void)userdata;
    if (eol > 0 || !i) return;

    pa_cvolume cv = i->volume;
    int vol = (pa_cvolume_avg(&cv) * 100) / PA_VOLUME_NORM;

    gboolean over_amp = get_overamplification();
    osd_logic_state_set_max_volume(over_amp ? 150 : 100);

    if (vol > osd_logic_state_get_max_volume()) vol = osd_logic_state_get_max_volume();

    int changed = 0;
    if (osd_logic_state_get_volume() != -1 &&
        (osd_logic_state_get_volume() != vol || osd_logic_state_get_muted() != i->mute)) {
        changed = 1;
    }

    osd_logic_state_set_volume(vol);
    osd_logic_state_set_muted(i->mute);

    if (changed) {
        osd_logic_state_set_type(OSD_VOLUME);
        printf("[PA] Sink volume updated: %d%%, muted: %d\n", vol, i->mute);
        osd_logic_state_show_osd();
    }
}

static void source_info_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    (void)c;
    (void)userdata;
    if (eol > 0 || !i) return;

    if (default_source_name[0] && g_strcmp0(i->name, default_source_name) != 0) {
        return;
    }

    if (osd_logic_state_get_mic_muted() == -1) {
        osd_logic_state_set_mic_muted(i->mute);
        osd_logic_state_set_mic_text(i->mute ? "Muted" : "Live");
        return;
    }

    refresh_mic_osd(i->mute);
}

static void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata) {
    (void)userdata;
    if (!i) return;
    printf("[PA] Default sink is %s\n", i->default_sink_name);
    g_strlcpy(default_source_name, i->default_source_name ? i->default_source_name : "", sizeof(default_source_name));
    pa_context_get_sink_info_by_name(c, i->default_sink_name, sink_info_cb, NULL);
    if (default_source_name[0]) {
        printf("[PA] Default source is %s\n", default_source_name);
        pa_context_get_source_info_by_name(c, default_source_name, source_info_cb, NULL);
    }
}

static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *userdata) {
    (void)index;
    (void)userdata;
    pa_subscription_event_type_t event_type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
    pa_subscription_event_type_t facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;

    if (event_type == PA_SUBSCRIPTION_EVENT_CHANGE &&
        (facility == PA_SUBSCRIPTION_EVENT_SINK || facility == PA_SUBSCRIPTION_EVENT_SOURCE || facility == PA_SUBSCRIPTION_EVENT_SERVER)) {
        printf("[PA] Audio event received, fetching info...\n");
        pa_context_get_server_info(c, server_info_cb, NULL);
    }
}

static void context_state_cb(pa_context *c, void *userdata) {
    (void)userdata;
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            printf("[PA] Context Ready\n");
            pa_context_get_server_info(c, server_info_cb, NULL);
            pa_context_set_subscribe_callback(c, subscribe_cb, NULL);
            pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL);
            break;
        case PA_CONTEXT_FAILED:
            printf("[PA] Context Failed\n");
            break;
        case PA_CONTEXT_TERMINATED:
            printf("[PA] Context Terminated\n");
            break;
        default:
            printf("[PA] Context State Changed: %d\n", pa_context_get_state(c));
            break;
    }
}

void osd_logic_audio_setup_pulseaudio(void) {
    my_pa_mainloop = pa_glib_mainloop_new(NULL);
    pa_mainloop_api *api = pa_glib_mainloop_get_api(my_pa_mainloop);
    pa_ctx = pa_context_new(api, "Venom OSD");

    pa_context_set_state_callback(pa_ctx, context_state_cb, NULL);
    pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
}
