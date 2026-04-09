/*
 * desktop_config.h
 * Shared desktop configuration and persisted state.
 */

#ifndef DESKTOP_CONFIG_H
#define DESKTOP_CONFIG_H

#include <gtk/gtk.h>

typedef enum {
    MODE_NORMAL,
    MODE_WORK,
    MODE_WIDGETS
} DesktopMode;

typedef enum {
    SORT_MANUAL,
    SORT_NAME,
    SORT_TYPE,
    SORT_DATE_MODIFIED,
    SORT_SIZE
} DesktopSortMode;

#define ICON_SIZE 56
#define ITEM_WIDTH 120
#define ITEM_HEIGHT 128
#define GRID_X 20
#define GRID_Y 20
#define CONFIG_FILE "/home/x/.config/vaxp/desktop-items.ini"
#define SORT_CONFIG_FILE "/home/x/.config/vaxp/desktop-sort"

void ensure_config_dir(void);

DesktopMode get_current_desktop_mode(void);
void set_current_desktop_mode(DesktopMode mode);

DesktopSortMode get_current_sort_mode(void);
void set_current_sort_mode(DesktopSortMode mode);
void sort_mode_to_markup(DesktopSortMode target_mode, GtkWidget *item);

char *get_current_desktop_path(void);
void save_item_position(const char *filename, int x, int y);
gboolean get_item_position(const char *filename, int *x, int *y);

#endif
