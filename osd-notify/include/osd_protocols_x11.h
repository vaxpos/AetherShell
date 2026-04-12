#ifndef VENOM_GUI_OSD_PROTOCOLS_X11_H
#define VENOM_GUI_OSD_PROTOCOLS_X11_H

#include "osd_protocols.h"

gboolean osd_protocols_x11_init(const OsdProtocolCallbacks *callbacks);
void osd_protocols_x11_shutdown(void);

#endif
