#include <gtk/gtk.h>
#if defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#endif

#include "osd_logic.h"
#include "osd_protocols.h"
#include "osd_ui.h"

static OsdUi osd_ui;

static gboolean is_wayland_session(void) {
#if defined(GDK_WINDOWING_WAYLAND)
    return GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default());
#else
    return FALSE;
#endif
}

static void osd_show_adapter(int width, int height, guint duration_ms) {
    osd_ui_show(&osd_ui, width, height, duration_ms);
}

#include "osd.h"

void osd_init(void) {
    osd_ui_init(&osd_ui, G_CALLBACK(osd_logic_on_draw), NULL);
    osd_logic_init(osd_show_adapter);
    osd_logic_setup_pulseaudio();

    OsdProtocolCallbacks callbacks = osd_logic_build_protocol_callbacks(is_wayland_session);
    osd_protocols_init(&callbacks);

    osd_logic_setup_brightness_monitoring();
}
