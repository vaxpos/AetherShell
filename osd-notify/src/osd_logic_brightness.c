#include "osd_logic_brightness.h"
#include "osd_logic_state.h"
#include <glib.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

static char brightness_device_dir[PATH_MAX] = "";
static char brightness_path[PATH_MAX] = "";
static char actual_brightness_path[PATH_MAX] = "";
static char max_brightness_path[PATH_MAX] = "";
static int brightness_watch_dir = -1;
static int brightness_watch_file = -1;
static int actual_brightness_watch_file = -1;

static gboolean discover_backlight_paths(void) {
    DIR *dir = opendir("/sys/class/backlight");
    if (!dir) return FALSE;

    memset(brightness_device_dir, 0, sizeof(brightness_device_dir));
    memset(brightness_path, 0, sizeof(brightness_path));
    memset(actual_brightness_path, 0, sizeof(actual_brightness_path));
    memset(max_brightness_path, 0, sizeof(max_brightness_path));

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char candidate_dir[PATH_MAX];
        char candidate_brightness[PATH_MAX];
        char candidate_actual[PATH_MAX];
        char candidate_max[PATH_MAX];

        g_strlcpy(candidate_dir, "/sys/class/backlight/", sizeof(candidate_dir));
        g_strlcat(candidate_dir, entry->d_name, sizeof(candidate_dir));

        g_strlcpy(candidate_brightness, candidate_dir, sizeof(candidate_brightness));
        g_strlcat(candidate_brightness, "/brightness", sizeof(candidate_brightness));

        g_strlcpy(candidate_actual, candidate_dir, sizeof(candidate_actual));
        g_strlcat(candidate_actual, "/actual_brightness", sizeof(candidate_actual));

        g_strlcpy(candidate_max, candidate_dir, sizeof(candidate_max));
        g_strlcat(candidate_max, "/max_brightness", sizeof(candidate_max));

        if (access(candidate_brightness, R_OK) != 0 || access(candidate_max, R_OK) != 0) {
            continue;
        }

        g_strlcpy(brightness_device_dir, candidate_dir, sizeof(brightness_device_dir));
        g_strlcpy(brightness_path, candidate_brightness, sizeof(brightness_path));
        g_strlcpy(max_brightness_path, candidate_max, sizeof(max_brightness_path));

        if (access(candidate_actual, R_OK) == 0) {
            g_strlcpy(actual_brightness_path, candidate_actual, sizeof(actual_brightness_path));
        }

        closedir(dir);
        printf("[Brightness] Using backlight device: %s\n", brightness_device_dir);
        return TRUE;
    }

    closedir(dir);
    return FALSE;
}

static gboolean read_brightness_percentage(int *percentage) {
    if (!percentage) return FALSE;

    if (!brightness_path[0] || !max_brightness_path[0]) {
        if (!discover_backlight_paths()) {
            return FALSE;
        }
    }

    const char *level_path = actual_brightness_path[0] ? actual_brightness_path : brightness_path;
    FILE *f = fopen(level_path, "r");
    FILE *fm = fopen(max_brightness_path, "r");
    if (!f || !fm) {
        if (f) fclose(f);
        if (fm) fclose(fm);
        return FALSE;
    }

    int level = 0;
    int max_level = 1;
    gboolean ok = FALSE;

    if (fscanf(f, "%d", &level) == 1 &&
        fscanf(fm, "%d", &max_level) == 1 &&
        max_level > 0) {
        int value = (level * 100) / max_level;
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        *percentage = value;
        ok = TRUE;
    }

    fclose(f);
    fclose(fm);
    return ok;
}

static void refresh_brightness_osd(gboolean force_show) {
    int percentage = 0;
    if (!read_brightness_percentage(&percentage)) {
        printf("[Brightness] Could not read brightness from sysfs\n");
        return;
    }

    if (!force_show &&
        osd_logic_state_get_brightness() == percentage &&
        osd_logic_state_get_type() == OSD_BRIGHTNESS) {
        return;
    }

    osd_logic_state_set_brightness(percentage);
    osd_logic_state_set_type(OSD_BRIGHTNESS);
    printf("[Brightness] Level %d%%\n", percentage);
    osd_logic_state_show_osd();
}

static gboolean on_inotify(GIOChannel *source, GIOCondition cond, gpointer data) {
    (void)cond; (void)data;
    char buf[BUF_LEN];
    gsize bytes_read;
    g_io_channel_read_chars(source, buf, BUF_LEN, &bytes_read, NULL);

    if (bytes_read > 0) {
        for (gsize offset = 0; offset < bytes_read;) {
            struct inotify_event *event = (struct inotify_event *)(buf + offset);
            gboolean is_brightness_event = FALSE;

            if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE)) {
                if (event->wd == brightness_watch_file || event->wd == actual_brightness_watch_file) {
                    is_brightness_event = TRUE;
                } else if (event->wd == brightness_watch_dir && event->len > 0 &&
                           (strcmp(event->name, "brightness") == 0 ||
                            strcmp(event->name, "actual_brightness") == 0)) {
                    is_brightness_event = TRUE;
                }
            }

            if (is_brightness_event) {
                printf("[Brightness] Modified event received\n");
                refresh_brightness_osd(FALSE);
            }

            offset += EVENT_SIZE + event->len;
        }
    }
    return TRUE;
}

void osd_logic_brightness_setup_monitoring(void) {
    int inotifyFd = inotify_init1(IN_NONBLOCK);
    if (inotifyFd < 0) {
        printf("[Brightness] Failed to initialize inotify\n");
        return;
    }

    if (!discover_backlight_paths()) {
        printf("[Brightness] No usable backlight device found in /sys/class/backlight\n");
        close(inotifyFd);
        return;
    }

    brightness_watch_dir = inotify_add_watch(inotifyFd, brightness_device_dir, IN_MODIFY | IN_CLOSE_WRITE);
    brightness_watch_file = inotify_add_watch(inotifyFd, brightness_path, IN_MODIFY | IN_CLOSE_WRITE);
    if (actual_brightness_path[0]) {
        actual_brightness_watch_file = inotify_add_watch(inotifyFd, actual_brightness_path, IN_MODIFY | IN_CLOSE_WRITE);
    }

    if (brightness_watch_dir < 0 && brightness_watch_file < 0 && actual_brightness_watch_file < 0) {
        printf("[Brightness] Failed to register inotify watches\n");
        close(inotifyFd);
        return;
    }

    GIOChannel *channel = g_io_channel_unix_new(inotifyFd);
    g_io_channel_set_encoding(channel, NULL, NULL);
    g_io_channel_set_buffered(channel, FALSE);
    g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL, on_inotify, NULL);
    g_io_channel_unref(channel);

    refresh_brightness_osd(TRUE);
}
