#include "utils.h"
#include <gtk/gtk.h>
#include <cairo.h>
#include <stdlib.h>

static gchar *dock_resource_base_dir = NULL;

static void cairo_rounded_rect(cairo_t *cr, gdouble x, gdouble y, gdouble width, gdouble height, gdouble radius) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -G_PI / 2.0, 0);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, G_PI / 2.0);
    cairo_arc(cr, x + radius, y + height - radius, radius, G_PI / 2.0, G_PI);
    cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3.0 * G_PI / 2.0);
    cairo_close_path(cr);
}

void open_with_preferred_file_manager(const gchar *path) {
    if (!path) return;

    gchar *cmd = NULL;

    /* Check vafile */
    gchar *vafile_path = g_find_program_in_path("vafile");
    if (vafile_path) {
        cmd = g_strdup("vafile");
        g_free(vafile_path);
    } else {
        /* Check nautilus */
        gchar *nautilus_path = g_find_program_in_path("nautilus");
        if (nautilus_path) {
            cmd = g_strdup("nautilus");
            g_free(nautilus_path);
        } else {
            /* Fallback */
            cmd = g_strdup("xdg-open");
        }
    }

    GError *error = NULL;
    gchar *argv[] = { cmd, (gchar *)path, NULL };
    
    if (!g_spawn_async(NULL, argv, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                       NULL, NULL, NULL, &error)) {
        g_warning("Failed to open path %s with %s: %s", path, cmd, error->message);
        g_error_free(error);
    }

    g_free(cmd);
}

const gchar *dock_get_resource_base_dir(void) {
    if (dock_resource_base_dir == NULL) {
        gchar *exe_path = g_file_read_link("/proc/self/exe", NULL);

        if (exe_path != NULL) {
            dock_resource_base_dir = g_path_get_dirname(exe_path);
            g_free(exe_path);
        } else {
            dock_resource_base_dir = g_get_current_dir();
        }
    }

    return dock_resource_base_dir;
}

gchar *dock_build_resource_path(const gchar *filename) {
    if (filename == NULL || *filename == '\0') {
        return NULL;
    }

    return g_build_filename(dock_get_resource_base_dir(), filename, NULL);
}

GdkPixbuf *create_rounded_icon_pixbuf(GdkPixbuf *source, gint size, gdouble radius) {
    GdkPixbuf *trimmed = NULL;
    GdkPixbuf *scaled;
    cairo_surface_t *surface;
    cairo_t *cr;
    GdkPixbuf *rounded;
    gint src_width;
    gint src_height;
    gint rowstride;
    gint channels;
    guchar *pixels;
    gint min_x;
    gint min_y;
    gint max_x;
    gint max_y;

    if (!source || size <= 0) return NULL;

    src_width = gdk_pixbuf_get_width(source);
    src_height = gdk_pixbuf_get_height(source);
    rowstride = gdk_pixbuf_get_rowstride(source);
    channels = gdk_pixbuf_get_n_channels(source);
    pixels = gdk_pixbuf_get_pixels(source);
    min_x = src_width;
    min_y = src_height;
    max_x = -1;
    max_y = -1;

    if (gdk_pixbuf_get_has_alpha(source) && channels >= 4) {
        for (gint y = 0; y < src_height; y++) {
            for (gint x = 0; x < src_width; x++) {
                guchar *p = pixels + y * rowstride + x * channels;
                if (p[3] > 12) {
                    if (x < min_x) min_x = x;
                    if (y < min_y) min_y = y;
                    if (x > max_x) max_x = x;
                    if (y > max_y) max_y = y;
                }
            }
        }
    }

    if (max_x >= min_x && max_y >= min_y) {
        gint crop_w = max_x - min_x + 1;
        gint crop_h = max_y - min_y + 1;
        trimmed = gdk_pixbuf_new_subpixbuf(source, min_x, min_y, crop_w, crop_h);
    } else {
        trimmed = g_object_ref(source);
    }

    scaled = gdk_pixbuf_scale_simple(trimmed, size, size, GDK_INTERP_BILINEAR);
    g_object_unref(trimmed);
    if (!scaled) return NULL;

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cr = cairo_create(surface);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_rounded_rect(cr, 0, 0, size, size, radius);
    cairo_clip(cr);
    gdk_cairo_set_source_pixbuf(cr, scaled, 0, 0);
    cairo_paint(cr);

    rounded = gdk_pixbuf_get_from_surface(surface, 0, 0, size, size);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(scaled);

    return rounded;
}
