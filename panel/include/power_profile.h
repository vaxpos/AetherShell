#ifndef POWER_PROFILE_H
#define POWER_PROFILE_H

#include <glib.h>

typedef void (*PowerProfileChangedCallback)(const char *profile, gpointer user_data);

void power_profile_init(PowerProfileChangedCallback cb, gpointer user_data);
void power_profile_set(const char *profile);

#endif // POWER_PROFILE_H
