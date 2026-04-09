/*
 * pulse_volume.c
 *
 * Bridges PulseAudio (via libpulse glib mainloop) with the panel:
 *   - Tracks the default sink (system volume)
 *   - Tracks per-application sink-inputs (volume mixer)
 */

#include "pulse_volume.h"
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <stdio.h>
#include <string.h>

/* ── Shared context ──────────────────────────────────────────────────────── */

static pa_glib_mainloop *m_glib    = NULL;
static pa_context       *m_context = NULL;
static pa_mainloop_api  *m_api     = NULL;

/* ── Master sink ─────────────────────────────────────────────────────────── */

static VolumeChangedCallback  m_vol_cb       = NULL;
static gpointer               m_vol_cb_data  = NULL;
static uint32_t               m_sink_idx     = PA_INVALID_INDEX;
static char                  *m_sink_name    = NULL;
static pa_cvolume             m_sink_vol;
static int                    m_current_pct  = -1;
static gboolean               m_current_muted = FALSE;

int pulse_volume_get_current(void) { return m_current_pct; }
gboolean pulse_volume_is_muted(void) { return m_current_muted; }

/* ── Master source (microphone) ──────────────────────────────────────────── */

static MicVolumeChangedCallback  m_mic_cb       = NULL;
static gpointer                  m_mic_cb_data  = NULL;
static uint32_t                  m_source_idx   = PA_INVALID_INDEX;
static char                     *m_source_name  = NULL;
static pa_cvolume                m_source_vol;
static int                       m_mic_current_pct  = -1;
static gboolean                  m_mic_current_muted = FALSE;

int pulse_mic_get_current(void) { return m_mic_current_pct; }
gboolean pulse_mic_is_muted(void) { return m_mic_current_muted; }

/* ── Sink-inputs ─────────────────────────────────────────────────────────── */

static SinkInputsChangedCallback m_si_cb      = NULL;
static gpointer                  m_si_cb_data = NULL;
static GList                    *m_si_list    = NULL;  /* SinkInputInfo* */

void pulse_sink_inputs_free(GList *list)
{
    for (GList *l = list; l; l = l->next) {
        SinkInputInfo *si = l->data;
        g_free(si->app_name);
        g_free(si);
    }
    g_list_free(list);
}

/* Deep-copy the current list for the callback */
static GList *clone_si_list(GList *src)
{
    GList *out = NULL;
    for (GList *l = src; l; l = l->next) {
        SinkInputInfo *orig = l->data;
        SinkInputInfo *copy = g_new0(SinkInputInfo, 1);
        *copy = *orig;
        copy->app_name = g_strdup(orig->app_name);
        out = g_list_append(out, copy);
    }
    return out;
}

static void notify_si_callback(void)
{
    if (m_si_cb) {
        GList *copy = clone_si_list(m_si_list);
        m_si_cb(copy, m_si_cb_data);
        /* Ownership of copy passes to the caller */
    }
}

/* ── Source-outputs ──────────────────────────────────────────────────────── */

static SourceOutputsChangedCallback m_so_cb      = NULL;
static gpointer                     m_so_cb_data = NULL;
static GList                       *m_so_list    = NULL;  /* SourceOutputInfo* */

void pulse_source_outputs_free(GList *list)
{
    for (GList *l = list; l; l = l->next) {
        SourceOutputInfo *si = l->data;
        g_free(si->app_name);
        g_free(si);
    }
    g_list_free(list);
}

static GList *clone_so_list(GList *src)
{
    GList *out = NULL;
    for (GList *l = src; l; l = l->next) {
        SourceOutputInfo *orig = l->data;
        SourceOutputInfo *copy = g_new0(SourceOutputInfo, 1);
        *copy = *orig;
        copy->app_name = g_strdup(orig->app_name);
        out = g_list_append(out, copy);
    }
    return out;
}

static void notify_so_callback(void)
{
    if (m_so_cb) {
        GList *copy = clone_so_list(m_so_list);
        m_so_cb(copy, m_so_cb_data);
    }
}

/* ── PulseAudio callbacks ─────────────────────────────────────────────────── */

static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
    (void)c; (void)userdata;
    if (eol > 0 || !i) return;

    m_sink_idx  = i->index;
    m_sink_vol  = i->volume;
    pa_volume_t avg = pa_cvolume_avg(&i->volume);
    m_current_pct = (int)((avg * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM);
    if (m_current_pct < 0)   m_current_pct = 0;
    if (m_current_pct > 150) m_current_pct = 150;
    
    m_current_muted = (gboolean)i->mute;

    if (m_vol_cb) m_vol_cb(m_current_pct, m_current_muted, m_vol_cb_data);
}

static void source_info_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
    (void)c; (void)userdata;
    if (eol > 0 || !i) return;

    m_source_idx  = i->index;
    m_source_vol  = i->volume;
    pa_volume_t avg = pa_cvolume_avg(&i->volume);
    m_mic_current_pct = (int)((avg * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM);
    if (m_mic_current_pct < 0)   m_mic_current_pct = 0;
    if (m_mic_current_pct > 150) m_mic_current_pct = 150;
    
    m_mic_current_muted = (gboolean)i->mute;

    if (m_mic_cb) m_mic_cb(m_mic_current_pct, m_mic_current_muted, m_mic_cb_data);
}

static void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata)
{
    (void)userdata;
    if (!i) return;

    if (i->default_sink_name) {
        if (m_sink_name) pa_xfree(m_sink_name);
        m_sink_name = pa_xstrdup(i->default_sink_name);
        pa_operation *o = pa_context_get_sink_info_by_name(c, m_sink_name, sink_info_cb, NULL);
        if (o) pa_operation_unref(o);
    }

    if (i->default_source_name) {
        if (m_source_name) pa_xfree(m_source_name);
        m_source_name = pa_xstrdup(i->default_source_name);
        pa_operation *o = pa_context_get_source_info_by_name(c, m_source_name, source_info_cb, NULL);
        if (o) pa_operation_unref(o);
    }
}

/* Rebuild internal sink-input list from a single pa_sink_input_info */
static void sink_input_info_cb(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata)
{
    (void)c;
    gboolean full_refresh = GPOINTER_TO_INT(userdata);

    if (eol > 0) {
        /* End of list — notify */
        notify_si_callback();
        return;
    }
    if (!i) return;

    /* Skip sink-inputs without a real app name (e.g. peak detect monitors) */
    const char *app = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
    if (!app || !*app) app = "Unknown";

    /* On full refresh the list was cleared before this callback chain started */
    /* On single-item update, remove the old entry if present */
    if (!full_refresh) {
        for (GList *l = m_si_list; l; l = l->next) {
            SinkInputInfo *si = l->data;
            if (si->index == i->index) {
                g_free(si->app_name);
                g_free(si);
                m_si_list = g_list_delete_link(m_si_list, l);
                break;
            }
        }
    }

    pa_volume_t avg = pa_cvolume_avg(&i->volume);
    int pct = (int)((avg * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM);
    if (pct < 0)   pct = 0;
    if (pct > 150) pct = 150;

    SinkInputInfo *si = g_new0(SinkInputInfo, 1);
    si->index          = i->index;
    si->app_name       = g_strdup(app);
    si->volume_percent = pct;
    si->muted          = (gboolean)i->mute;

    m_si_list = g_list_append(m_si_list, si);
}

static void refresh_sink_inputs(pa_context *c)
{
    /* Clear and refetch everything */
    pulse_sink_inputs_free(m_si_list);
    m_si_list = NULL;
    pa_operation *o = pa_context_get_sink_input_info_list(c, sink_input_info_cb, GINT_TO_POINTER(TRUE));
    if (o) pa_operation_unref(o);
}

static void source_output_info_cb(pa_context *c, const pa_source_output_info *i, int eol, void *userdata)
{
    (void)c;
    gboolean full_refresh = GPOINTER_TO_INT(userdata);

    if (eol > 0) {
        notify_so_callback();
        return;
    }
    if (!i) return;

    const char *app = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
    if (!app || !*app) app = "Unknown";

    if (!full_refresh) {
        for (GList *l = m_so_list; l; l = l->next) {
            SourceOutputInfo *si = l->data;
            if (si->index == i->index) {
                g_free(si->app_name);
                g_free(si);
                m_so_list = g_list_delete_link(m_so_list, l);
                break;
            }
        }
    }

    pa_volume_t avg = pa_cvolume_avg(&i->volume);
    int pct = (int)((avg * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM);
    if (pct < 0)   pct = 0;
    if (pct > 150) pct = 150;

    SourceOutputInfo *si = g_new0(SourceOutputInfo, 1);
    si->index          = i->index;
    si->app_name       = g_strdup(app);
    si->volume_percent = pct;
    si->muted          = (gboolean)i->mute;

    m_so_list = g_list_append(m_so_list, si);
}

static void refresh_source_outputs(pa_context *c)
{
    pulse_source_outputs_free(m_so_list);
    m_so_list = NULL;
    pa_operation *o = pa_context_get_source_output_info_list(c, source_output_info_cb, GINT_TO_POINTER(TRUE));
    if (o) pa_operation_unref(o);
}

static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
    (void)userdata;
    pa_subscription_event_type_t fac = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    pa_subscription_event_type_t typ = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    if (fac == PA_SUBSCRIPTION_EVENT_SINK) {
        pa_operation *o = pa_context_get_sink_info_by_index(c, idx, sink_info_cb, NULL);
        if (o) pa_operation_unref(o);
    } else if (fac == PA_SUBSCRIPTION_EVENT_SERVER) {
        pa_operation *o = pa_context_get_server_info(c, server_info_cb, NULL);
        if (o) pa_operation_unref(o);
    } else if (fac == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
        if (typ == PA_SUBSCRIPTION_EVENT_REMOVE) {
            /* Remove from list directly */
            for (GList *l = m_si_list; l; l = l->next) {
                SinkInputInfo *si = l->data;
                if (si->index == idx) {
                    g_free(si->app_name);
                    g_free(si);
                    m_si_list = g_list_delete_link(m_si_list, l);
                    break;
                }
            }
            notify_si_callback();
        } else {
            /* New or changed — fetch the single item */
            pa_operation *o = pa_context_get_sink_input_info(c, idx, sink_input_info_cb, GINT_TO_POINTER(FALSE));
            if (o) pa_operation_unref(o);
        }
    } else if (fac == PA_SUBSCRIPTION_EVENT_SOURCE) {
        pa_operation *o = pa_context_get_source_info_by_index(c, idx, source_info_cb, NULL);
        if (o) pa_operation_unref(o);
    } else if (fac == PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT) {
        if (typ == PA_SUBSCRIPTION_EVENT_REMOVE) {
            for (GList *l = m_so_list; l; l = l->next) {
                SourceOutputInfo *si = l->data;
                if (si->index == idx) {
                    g_free(si->app_name);
                    g_free(si);
                    m_so_list = g_list_delete_link(m_so_list, l);
                    break;
                }
            }
            notify_so_callback();
        } else {
            pa_operation *o = pa_context_get_source_output_info(c, idx, source_output_info_cb, GINT_TO_POINTER(FALSE));
            if (o) pa_operation_unref(o);
        }
    }
}

static void context_state_cb(pa_context *c, void *userdata)
{
    (void)userdata;
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY: {
            pa_context_set_subscribe_callback(c, subscribe_cb, NULL);
            pa_operation *o = pa_context_subscribe(c,
                (pa_subscription_mask_t)(PA_SUBSCRIPTION_MASK_SINK |
                                        PA_SUBSCRIPTION_MASK_SERVER |
                                        PA_SUBSCRIPTION_MASK_SINK_INPUT |
                                        PA_SUBSCRIPTION_MASK_SOURCE |
                                        PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT),
                NULL, NULL);
            if (o) pa_operation_unref(o);

            o = pa_context_get_server_info(c, server_info_cb, NULL);
            if (o) pa_operation_unref(o);

            refresh_sink_inputs(c);
            refresh_source_outputs(c);
            break;
        }
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            break;
        default:
            break;
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void pulse_volume_init(VolumeChangedCallback cb, gpointer user_data)
{
    m_vol_cb      = cb;
    m_vol_cb_data = user_data;

    if (m_glib) return;   /* already initialised */

    m_glib    = pa_glib_mainloop_new(NULL);
    m_api     = pa_glib_mainloop_get_api(m_glib);
    m_context = pa_context_new(m_api, "ControlCenter");
    pa_context_set_state_callback(m_context, context_state_cb, NULL);
    pa_context_connect(m_context, NULL, PA_CONTEXT_NOFLAGS, NULL);
}

void pulse_sink_inputs_init(SinkInputsChangedCallback cb, gpointer user_data)
{
    m_si_cb      = cb;
    m_si_cb_data = user_data;
    /* Context is already set up by pulse_volume_init; if it's ready, do an
     * immediate refresh, otherwise the context_state_cb will do it. */
    if (m_context && pa_context_get_state(m_context) == PA_CONTEXT_READY) {
        refresh_sink_inputs(m_context);
    }
}

void pulse_volume_set(int percent)
{
    if (!m_context || pa_context_get_state(m_context) != PA_CONTEXT_READY) return;
    if (!m_sink_name) return;
    if (percent < 0)   percent = 0;
    if (percent > 150) percent = 150;

    pa_cvolume cv = m_sink_vol;
    pa_volume_t target = (pa_volume_t)(((long long)percent * PA_VOLUME_NORM) / 100);

    if (cv.channels == 0)
        pa_cvolume_set(&cv, 2, target);
    else
        pa_cvolume_set(&cv, cv.channels, target);

    pa_operation *o = pa_context_set_sink_volume_by_name(m_context, m_sink_name, &cv, NULL, NULL);
    if (o) pa_operation_unref(o);
}

void pulse_sink_input_set_volume(uint32_t index, int percent)
{
    if (!m_context || pa_context_get_state(m_context) != PA_CONTEXT_READY) return;
    if (percent < 0)   percent = 0;
    if (percent > 150) percent = 150;

    pa_cvolume cv;
    pa_cvolume_set(&cv, 2, (pa_volume_t)(((long long)percent * PA_VOLUME_NORM) / 100));
    pa_operation *o = pa_context_set_sink_input_volume(m_context, index, &cv, NULL, NULL);
    if (o) pa_operation_unref(o);
}

void pulse_sink_input_set_mute(uint32_t index, gboolean mute)
{
    if (!m_context || pa_context_get_state(m_context) != PA_CONTEXT_READY) return;
    pa_operation *o = pa_context_set_sink_input_mute(m_context, index, (int)mute, NULL, NULL);
    if (o) pa_operation_unref(o);
}

void pulse_mic_init(MicVolumeChangedCallback cb, gpointer user_data)
{
    m_mic_cb      = cb;
    m_mic_cb_data = user_data;
    // Context is initialised by pulse_volume_init. Do nothing else.
}

void pulse_source_outputs_init(SourceOutputsChangedCallback cb, gpointer user_data)
{
    m_so_cb      = cb;
    m_so_cb_data = user_data;
    if (m_context && pa_context_get_state(m_context) == PA_CONTEXT_READY) {
        refresh_source_outputs(m_context);
    }
}

void pulse_mic_set(int percent)
{
    if (!m_context || pa_context_get_state(m_context) != PA_CONTEXT_READY) return;
    if (!m_source_name) return;
    if (percent < 0)   percent = 0;
    if (percent > 150) percent = 150;

    pa_cvolume cv = m_source_vol;
    pa_volume_t target = (pa_volume_t)(((long long)percent * PA_VOLUME_NORM) / 100);

    if (cv.channels == 0)
        pa_cvolume_set(&cv, 2, target);
    else
        pa_cvolume_set(&cv, cv.channels, target);

    pa_operation *o = pa_context_set_source_volume_by_name(m_context, m_source_name, &cv, NULL, NULL);
    if (o) pa_operation_unref(o);
}

void pulse_source_output_set_volume(uint32_t index, int percent)
{
    if (!m_context || pa_context_get_state(m_context) != PA_CONTEXT_READY) return;
    if (percent < 0)   percent = 0;
    if (percent > 150) percent = 150;

    pa_cvolume cv;
    pa_cvolume_set(&cv, 2, (pa_volume_t)(((long long)percent * PA_VOLUME_NORM) / 100));
    pa_operation *o = pa_context_set_source_output_volume(m_context, index, &cv, NULL, NULL);
    if (o) pa_operation_unref(o);
}

void pulse_source_output_set_mute(uint32_t index, gboolean mute)
{
    if (!m_context || pa_context_get_state(m_context) != PA_CONTEXT_READY) return;
    pa_operation *o = pa_context_set_source_output_mute(m_context, index, (int)mute, NULL, NULL);
    if (o) pa_operation_unref(o);
}
