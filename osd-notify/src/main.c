#include <gtk/gtk.h>
#include "osd.h"
#include "notify.h"

int main(int argc, char *argv[]) {
    // تهيئة GTK
    gtk_init(&argc, &argv);

    g_print("🎨 Starting venom_gui (OSD + Notify)\n");

    // تهيئة نظام OSD للغات
    osd_init();

    // تهيئة نظام الإشعارات
    notify_init();

    // تشغيل حلقة GTK الأساسية المشتركة
    gtk_main();

    return 0;
}
