#ifndef MPRIS_CONTROL_H
#define MPRIS_CONTROL_H

#include <glib.h>

// Callbacks
typedef void (*MprisStateChangedCallback)(gboolean is_playing, const char *title, const char *artist, const char *art_url, gpointer user_data);

void mpris_control_init(MprisStateChangedCallback cb, gpointer user_data);
void mpris_control_play_pause(void);
void mpris_control_next(void);
void mpris_control_prev(void);

#endif // MPRIS_CONTROL_H
