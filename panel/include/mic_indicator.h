#ifndef MIC_INDICATOR_H
#define MIC_INDICATOR_H

#include <gtk/gtk.h>

/*
 * Creates the microphone indicator widget (button + icon) and
 * initializes the background mic mixer window.
 */
GtkWidget *create_mic_indicator_widget(void);

#endif // MIC_INDICATOR_H
