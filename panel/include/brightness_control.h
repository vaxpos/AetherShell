#ifndef BRIGHTNESS_CONTROL_H
#define BRIGHTNESS_CONTROL_H

#include <glib.h>

typedef void (*BrightnessChangedCallback)(int percent, gpointer user_data);

/* Initializes brightness control and reads the current value.
 * cb will be called once immediately with the current brightness,
 * and again whenever the brightness changes externally. */
void brightness_init(BrightnessChangedCallback cb, gpointer user_data);

/* Sets brightness (0-100 percent). */
void brightness_set(int percent);

/* Returns the current brightness percent (0-100), or -1 on error. */
int  brightness_get(void);

#endif // BRIGHTNESS_CONTROL_H
