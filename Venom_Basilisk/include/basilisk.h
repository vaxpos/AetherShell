/*
 * ═══════════════════════════════════════════════════════════════════════════
 * 🔍 Venom Basilisk - Global Header
 * ═══════════════════════════════════════════════════════════════════════════
 */

#ifndef BASILISK_H
#define BASILISK_H

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

// D-Bus
#define DBUS_NAME "org.venom.Basilisk"
#define DBUS_PATH "/org/venom/Basilisk"
#define DBUS_INTERFACE "org.venom.Basilisk"

// Window dimensions
#define WINDOW_WIDTH 650
#define WINDOW_HEIGHT 500
#define GRID_COLS 5
#define GRID_ROWS 3
#define ICON_SIZE 64

// Categories
typedef enum {
    CAT_ALL = 0,
    CAT_DEVELOPMENT,
    CAT_SYSTEM,
    CAT_INTERNET,
    CAT_UTILITY,
    CAT_OTHER,
    CAT_COUNT
} AppCategory;

// App Entry
typedef struct {
    gchar *name;
    gchar *exec;
    gchar *icon;
    gchar *desktop_file;
    AppCategory category;
} AppEntry;

// Global state
typedef struct {
    GtkWidget *window;
    GtkWidget *search_entry;
    GtkWidget *app_grid;
    GtkWidget *category_bar;
    GtkWidget *scroll_window;
    GDBusConnection *dbus_conn;
    guint dbus_owner_id;
    GList *app_cache;
    gboolean visible;
    gboolean search_hint_visible;
    AppCategory current_category;
} BasiliskState;

extern BasiliskState *state;

#endif
