#include "mpris_control.h"
#include <gio/gio.h>

static GDBusConnection *m_bus = NULL;
static guint m_name_owner_id = 0;
static char *m_active_player = NULL;
static MprisStateChangedCallback m_callback = NULL;
static gpointer m_callback_data = NULL;

static gboolean m_is_playing = FALSE;
static char *m_song_title = NULL;
static char *m_song_artist = NULL;
static char *m_song_art_url = NULL;

static void notify_state() {
    if (m_callback) {
        m_callback(m_is_playing, m_song_title, m_song_artist, m_song_art_url, m_callback_data);
    }
}

static gchar* extract_string(GVariant *dict, const char *key) {
    if (!dict) return NULL;
    GVariant *v = g_variant_lookup_value(dict, key, NULL);
    if (!v) return NULL;
    GVariant *real_val = v;
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
        real_val = g_variant_get_variant(v);
        g_variant_unref(v);
    }
    gchar *result = NULL;
    if (g_variant_is_of_type(real_val, G_VARIANT_TYPE_STRING)) {
        result = g_strdup(g_variant_get_string(real_val, NULL));
    }
    g_variant_unref(real_val);
    return result;
}

static gchar* extract_first_array_string(GVariant *dict, const char *key) {
    if (!dict) return NULL;
    GVariant *v = g_variant_lookup_value(dict, key, NULL);
    if (!v) return NULL;
    GVariant *real_val = v;
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
        real_val = g_variant_get_variant(v);
        g_variant_unref(v);
    }
    gchar *result = NULL;
    if (g_variant_is_of_type(real_val, G_VARIANT_TYPE_STRING_ARRAY)) {
        GVariantIter iter;
        g_variant_iter_init(&iter, real_val);
        gchar *str = NULL;
        if (g_variant_iter_next(&iter, "s", &str)) {
            result = str;
            gchar *dummy;
            while (g_variant_iter_next(&iter, "s", &dummy)) g_free(dummy);
        }
    }
    g_variant_unref(real_val);
    return result;
}

static void get_mpris_properties() {
    if (!m_active_player || !m_bus) return;
    GError *error = NULL;
    GVariant *res = g_dbus_connection_call_sync(m_bus,
                                                m_active_player,
                                                "/org/mpris/MediaPlayer2",
                                                "org.freedesktop.DBus.Properties",
                                                "GetAll",
                                                g_variant_new("(s)", "org.mpris.MediaPlayer2.Player"),
                                                G_VARIANT_TYPE("(a{sv})"),
                                                G_DBUS_CALL_FLAGS_NONE,
                                                -1, NULL, &error);
    if (error) { g_error_free(error); return; }
    if (res) {
        GVariant *dict = g_variant_get_child_value(res, 0);
        gchar *status_str = extract_string(dict, "PlaybackStatus");
        if (status_str) {
            m_is_playing = (g_strcmp0(status_str, "Playing") == 0);
            g_free(status_str);
        }
        GVariant *meta_v = g_variant_lookup_value(dict, "Metadata", NULL);
        if (meta_v) {
            GVariant *meta_dict = meta_v;
            if (g_variant_is_of_type(meta_v, G_VARIANT_TYPE_VARIANT)) {
                meta_dict = g_variant_get_variant(meta_v);
                g_variant_unref(meta_v);
            }
            gchar *title = extract_string(meta_dict, "xesam:title");
            if (title) {
                g_free(m_song_title);
                m_song_title = title;
            }
            gchar *artist = extract_first_array_string(meta_dict, "xesam:artist");
            if (artist) {
                g_free(m_song_artist);
                m_song_artist = artist;
            }
            gchar *art_url = extract_string(meta_dict, "mpris:artUrl");
            if (art_url) {
                g_free(m_song_art_url);
                m_song_art_url = art_url;
            } else {
                g_free(m_song_art_url);
                m_song_art_url = NULL;
            }
            g_variant_unref(meta_dict);
        }
        g_variant_unref(dict);
        g_variant_unref(res);
    }
    notify_state();
}

static void scan_players_and_update() {
    if (!m_bus) return;
    GError *error = NULL;
    GVariant *res = g_dbus_connection_call_sync(m_bus,
                                                "org.freedesktop.DBus",
                                                "/org/freedesktop/DBus",
                                                "org.freedesktop.DBus",
                                                "ListNames",
                                                NULL,
                                                G_VARIANT_TYPE("(as)"),
                                                G_DBUS_CALL_FLAGS_NONE,
                                                -1, NULL, &error);
    if (error) { g_error_free(error); return; }
    
    GVariantIter *iter;
    g_variant_get(res, "(as)", &iter);
    char *name;
    char *best_player = NULL;
    char *fallback_player = NULL;
    
    while (g_variant_iter_next(iter, "s", &name)) {
        if (g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) {
            GVariant *prop_res = g_dbus_connection_call_sync(m_bus, name,
                                          "/org/mpris/MediaPlayer2",
                                          "org.freedesktop.DBus.Properties",
                                          "Get",
                                          g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "PlaybackStatus"),
                                          G_VARIANT_TYPE("(v)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          500, NULL, NULL);
            if (prop_res) {
                GVariant *v_val;
                g_variant_get(prop_res, "(v)", &v_val);
                GVariant *unboxed = v_val;
                if (g_variant_is_of_type(v_val, G_VARIANT_TYPE_VARIANT)) {
                    unboxed = g_variant_get_variant(v_val);
                    g_variant_unref(v_val);
                }
                if (g_variant_is_of_type(unboxed, G_VARIANT_TYPE_STRING)) {
                    const gchar *status = g_variant_get_string(unboxed, NULL);
                    if (g_strcmp0(status, "Playing") == 0) {
                        best_player = g_strdup(name);
                        g_variant_unref(unboxed);
                        g_variant_unref(prop_res);
                        g_free(name);
                        break;
                    }
                }
                g_variant_unref(unboxed);
                g_variant_unref(prop_res);
            }
            if (!fallback_player) fallback_player = g_strdup(name);
        }
        g_free(name);
    }
    g_variant_iter_free(iter);
    g_variant_unref(res);
    
    char *selected_player = best_player ? best_player : fallback_player;
    if (g_strcmp0(m_active_player, selected_player) != 0) {
        g_free(m_active_player);
        m_active_player = g_strdup(selected_player);
    }
    if (best_player) g_free(best_player);
    if (fallback_player) g_free(fallback_player);
    
    if (!m_active_player) {
         g_free(m_song_title); m_song_title = NULL;
         g_free(m_song_artist); m_song_artist = NULL;
         g_free(m_song_art_url); m_song_art_url = NULL;
         m_is_playing = FALSE;
         notify_state();
    } else {
         get_mpris_properties();
    }
}

static void on_dbus_signal(GDBusConnection *connection,
                           const gchar *sender_name,
                           const gchar *object_path,
                           const gchar *interface_name,
                           const gchar *signal_name,
                           GVariant *parameters,
                           gpointer user_data) {
    if (g_strcmp0(signal_name, "PropertiesChanged") == 0) {
        if (m_active_player && g_strcmp0(sender_name, m_active_player) == 0) {
            const gchar *iface;
            GVariant *changed_props;
            GVariant *invalidated;
            g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed_props, &invalidated);
            if (g_strcmp0(iface, "org.mpris.MediaPlayer2.Player") == 0) {
                GVariantIter *iter;
                g_variant_get(changed_props, "a{sv}", &iter);
                const gchar *key;
                GVariant *value;
                gboolean metadata_changed = FALSE;
                while (g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
                    if (g_strcmp0(key, "PlaybackStatus") == 0) {
                        GVariant *unboxed = value;
                        if (g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT)) {
                            unboxed = g_variant_get_variant(value);
                        }
                        if (g_variant_is_of_type(unboxed, G_VARIANT_TYPE_STRING)) {
                            const gchar *status = g_variant_get_string(unboxed, NULL);
                            m_is_playing = (g_strcmp0(status, "Playing") == 0);
                        }
                        if (unboxed != value) g_variant_unref(unboxed);
                    } else if (g_strcmp0(key, "Metadata") == 0) {
                        metadata_changed = TRUE;
                    }
                }
                g_variant_iter_free(iter);
                if (metadata_changed) {
                    get_mpris_properties();
                } else {
                    notify_state();
                }
            }
            g_variant_unref(changed_props);
            g_variant_unref(invalidated);
            return;
        }
    }
    scan_players_and_update();
}

static void on_name_owner_changed(GDBusConnection *connection,
                                  const gchar *sender_name,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *signal_name,
                                  GVariant *parameters,
                                  gpointer user_data) {
    scan_players_and_update();
}

void mpris_control_init(MprisStateChangedCallback cb, gpointer user_data) {
    m_callback = cb;
    m_callback_data = user_data;
    
    GError *error = NULL;
    m_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!error && m_bus) {
        g_dbus_connection_signal_subscribe(m_bus, NULL,
                                           "org.freedesktop.DBus.Properties",
                                           "PropertiesChanged",
                                           NULL, "org.mpris.MediaPlayer2.Player",
                                           G_DBUS_SIGNAL_FLAGS_NONE, on_dbus_signal, NULL, NULL);
        m_name_owner_id = g_dbus_connection_signal_subscribe(m_bus,
                                           "org.freedesktop.DBus", "org.freedesktop.DBus",
                                           "NameOwnerChanged", "/org/freedesktop/DBus",
                                           NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_name_owner_changed, NULL, NULL);
        scan_players_and_update();
    } else if (error) {
        g_error_free(error);
    }
}

static void send_mpris_command(const char *method) {
    if (!m_active_player || !m_bus) return;
    g_dbus_connection_call(m_bus, m_active_player,
                           "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player",
                           method, NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

void mpris_control_play_pause(void) { send_mpris_command("PlayPause"); }
void mpris_control_next(void) { send_mpris_command("Next"); }
void mpris_control_prev(void) { send_mpris_command("Previous"); }
