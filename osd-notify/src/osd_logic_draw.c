#include "osd_logic_draw.h"
#include "osd_logic_state.h"
#include <math.h>
#include <stdio.h>

#define OSD_WIDTH 200
#define OSD_HEIGHT 200
#define FONT_SIZE 40.0
#define OSD_MARGIN 10.0
#define OSD_RADIUS 20.0

static void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double radius) {
    double degrees = M_PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc(cr, x + w - radius, y + h - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc(cr, x + radius, y + h - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path(cr);
}

static void draw_icon_speaker(cairo_t *cr, double cx, double cy, double size, int muted) {
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 4.0);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    cairo_move_to(cr, cx - size*0.4, cy - size*0.2);
    cairo_line_to(cr, cx - size*0.1, cy - size*0.2);
    cairo_line_to(cr, cx + size*0.2, cy - size*0.4);
    cairo_line_to(cr, cx + size*0.2, cy + size*0.4);
    cairo_line_to(cr, cx - size*0.1, cy + size*0.2);
    cairo_line_to(cr, cx - size*0.4, cy + size*0.2);
    cairo_close_path(cr);
    cairo_stroke_preserve(cr);
    cairo_fill(cr);

    if (muted) {
        cairo_set_source_rgb(cr, 1.0, 0.2, 0.2);
        cairo_move_to(cr, cx + size*0.1, cy - size*0.2);
        cairo_line_to(cr, cx + size*0.5, cy + size*0.2);
        cairo_move_to(cr, cx + size*0.5, cy - size*0.2);
        cairo_line_to(cr, cx + size*0.1, cy + size*0.2);
        cairo_stroke(cr);
    } else {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_arc(cr, cx + size*0.2, cy, size*0.2, -M_PI/4, M_PI/4);
        cairo_stroke(cr);
        cairo_arc(cr, cx + size*0.2, cy, size*0.4, -M_PI/4, M_PI/4);
        cairo_stroke(cr);
    }
}

static void draw_icon_sun(cairo_t *cr, double cx, double cy, double size) {
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 4.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    cairo_arc(cr, cx, cy, size*0.25, 0, 2*M_PI);
    cairo_stroke_preserve(cr);
    cairo_fill(cr);

    for (int i = 0; i < 8; ++i) {
        double angle = i * M_PI / 4.0;
        cairo_move_to(cr, cx + cos(angle) * size * 0.35, cy + sin(angle) * size * 0.35);
        cairo_line_to(cr, cx + cos(angle) * size * 0.5, cy + sin(angle) * size * 0.5);
        cairo_stroke(cr);
    }
}

static void draw_icon_microphone(cairo_t *cr, double cx, double cy, double size, int muted) {
    double body_w = size * 0.35;
    double body_h = size * 0.55;
    double radius = body_w / 2.0;

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 4.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    // capsule body
    draw_rounded_rect(cr, cx - body_w / 2.0, cy - body_h / 2.0, body_w, body_h, radius);
    cairo_stroke_preserve(cr);
    cairo_fill(cr);

    // stem
    cairo_move_to(cr, cx, cy + body_h / 2.0);
    cairo_line_to(cr, cx, cy + body_h / 2.0 + size * 0.18);
    cairo_stroke(cr);

    // base arc
    cairo_arc(cr, cx, cy + body_h / 2.0 + size * 0.22, body_w * 0.6, M_PI, 2 * M_PI);
    cairo_stroke(cr);

    if (muted) {
        cairo_set_source_rgb(cr, 1.0, 0.2, 0.2);
        cairo_set_line_width(cr, 4.0);
        cairo_move_to(cr, cx - size * 0.45, cy - size * 0.45);
        cairo_line_to(cr, cx + size * 0.45, cy + size * 0.45);
        cairo_stroke(cr);
    }
}

gboolean osd_logic_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    if (width <= 0) width = OSD_WIDTH;
    if (height <= 0) height = OSD_HEIGHT;

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    double x = OSD_MARGIN;
    double y = OSD_MARGIN;
    double w = width - (OSD_MARGIN * 2.0);
    double h = height - (OSD_MARGIN * 2.0);
    if (w < 1.0) w = width;
    if (h < 1.0) h = height;

    // Rounded background (old style)
    draw_rounded_rect(cr, x, y, w, h, OSD_RADIUS);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.3);
    cairo_fill(cr);

    cairo_save(cr);
    draw_rounded_rect(cr, x, y, w, h, OSD_RADIUS);
    cairo_clip(cr);

    OsdType type = osd_logic_state_get_type();
    if (type == OSD_KEYBOARD) {
        const char *text = osd_logic_state_get_text();
        cairo_set_source_rgb(cr, 0.0, 1.0, 1.0);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, FONT_SIZE);

        cairo_text_extents_t extents;
        cairo_text_extents(cr, text, &extents);
        double x = (width - extents.width) / 2.0 - extents.x_bearing;
        double y = (height - extents.height) / 2.0 - extents.y_bearing;
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, text);
    } else if (type == OSD_VOLUME || type == OSD_BRIGHTNESS || type == OSD_MIC) {
        double icon_x = width / 2.0;
        double icon_y = height / 2.0 - 20;
        double icon_size = 60;

        if (type == OSD_VOLUME) {
            draw_icon_speaker(cr, icon_x, icon_y, icon_size, osd_logic_state_get_muted());
        } else if (type == OSD_BRIGHTNESS) {
            draw_icon_sun(cr, icon_x, icon_y, icon_size);
        } else {
            draw_icon_microphone(cr, icon_x, icon_y, icon_size, osd_logic_state_get_mic_muted());
        }

        double percentage = 0;
        if (type == OSD_VOLUME) {
            percentage = osd_logic_state_get_volume();
        } else if (type == OSD_BRIGHTNESS) {
            percentage = osd_logic_state_get_brightness();
        } else {
            percentage = osd_logic_state_get_mic_muted() ? 0 : 100;
        }

        double bar_w = w * 0.8;
        double bar_h = 10.0;
        double bar_x = (width - bar_w) / 2.0;
        double bar_y = y + h - 30.0;

        draw_rounded_rect(cr, bar_x, bar_y, bar_w, bar_h, bar_h/2.0);
        cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 1.0);
        cairo_fill(cr);

        if (percentage > 0) {
            double max_val = (type == OSD_VOLUME) ? (double)osd_logic_state_get_max_volume() : 100.0;
            double fill_ratio = percentage / max_val;
            if (fill_ratio > 1.0) fill_ratio = 1.0;

            double fill_w = bar_w * fill_ratio;
            draw_rounded_rect(cr, bar_x, bar_y, fill_w, bar_h, bar_h/2.0);
            if (type == OSD_VOLUME && osd_logic_state_get_muted()) {
                cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
            } else {
                cairo_set_source_rgb(cr, 0.0, 1.0, 1.0);
            }
            cairo_fill(cr);
        }

        if (type != OSD_MIC) {
            char val_str[16];
            snprintf(val_str, sizeof(val_str), "%d%%", (int)percentage);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 16.0);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_text_extents_t extents;
            cairo_text_extents(cr, val_str, &extents);
            cairo_move_to(cr, (width - extents.width)/2.0 - extents.x_bearing, bar_y - 10.0);
            cairo_show_text(cr, val_str);
        }

        if (type == OSD_MIC) {
            const char *mic_text = osd_logic_state_get_mic_text();
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 16.0);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_text_extents_t extents;
            cairo_text_extents(cr, mic_text, &extents);
            cairo_move_to(cr, (width - extents.width)/2.0 - extents.x_bearing, bar_y - 10.0);
            cairo_show_text(cr, mic_text);
        }
    }

    cairo_restore(cr);
    return FALSE;
}
