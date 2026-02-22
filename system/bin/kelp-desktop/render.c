/*
 * kelp-desktop :: render.c
 * Cairo drawing primitives: rounded rect, shadow, gradient, text.
 *
 * SPDX-License-Identifier: MIT
 */

#include "render.h"
#include <math.h>
#include <string.h>

/* ---- Rounded rectangle -------------------------------------------------- */

void kd_rounded_rect(cairo_t *cr, double x, double y,
                      double w, double h, double radius)
{
    double deg = M_PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - radius, y + radius,     radius, -90 * deg,   0 * deg);
    cairo_arc(cr, x + w - radius, y + h - radius, radius,   0 * deg,  90 * deg);
    cairo_arc(cr, x + radius,     y + h - radius, radius,  90 * deg, 180 * deg);
    cairo_arc(cr, x + radius,     y + radius,     radius, 180 * deg, 270 * deg);
    cairo_close_path(cr);
}

void kd_fill_rounded_rect(cairo_t *cr, double x, double y,
                            double w, double h, double radius,
                            kd_color_t color)
{
    cairo_save(cr);
    kd_rounded_rect(cr, x, y, w, h, radius);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_fill(cr);
    cairo_restore(cr);
}

void kd_draw_shadow(cairo_t *cr, double x, double y,
                     double w, double h, double radius,
                     double blur, kd_color_t color)
{
    /* Simple multi-layer shadow approximation. */
    cairo_save(cr);
    for (int i = (int)blur; i > 0; i -= 2) {
        double alpha = color.a * (1.0 - (double)i / (blur + 1.0)) * 0.3;
        double offset = (double)i;
        kd_rounded_rect(cr, x - offset, y - offset,
                         w + offset * 2, h + offset * 2,
                         radius + offset);
        cairo_set_source_rgba(cr, color.r, color.g, color.b, alpha);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

void kd_draw_border(cairo_t *cr, double x, double y,
                     double w, double h, double radius,
                     kd_color_t color)
{
    cairo_save(cr);
    kd_rounded_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, radius);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void kd_fill_rect(cairo_t *cr, double x, double y,
                   double w, double h, kd_color_t color)
{
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_fill(cr);
    cairo_restore(cr);
}

void kd_draw_hline(cairo_t *cr, double x, double y,
                    double w, kd_color_t color)
{
    cairo_save(cr);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x, y + 0.5);
    cairo_line_to(cr, x + w, y + 0.5);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void kd_draw_vline(cairo_t *cr, double x, double y,
                    double h, kd_color_t color)
{
    cairo_save(cr);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x + 0.5, y);
    cairo_line_to(cr, x + 0.5, y + h);
    cairo_stroke(cr);
    cairo_restore(cr);
}

/* ---- Text drawing ------------------------------------------------------- */

static PangoLayout *make_layout(cairo_t *cr, const char *text,
                                 const char *font_family, double font_size,
                                 PangoWeight weight, double max_width)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, text, -1);

    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, font_family);
    pango_font_description_set_size(desc, (int)(font_size * PANGO_SCALE));
    pango_font_description_set_weight(desc, weight);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    if (max_width > 0) {
        pango_layout_set_width(layout, (int)(max_width * PANGO_SCALE));
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    }

    return layout;
}

int kd_draw_text(cairo_t *cr, const char *text, double x, double y,
                  const char *font_family, double font_size,
                  kd_color_t color, double max_width)
{
    cairo_save(cr);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_move_to(cr, x, y);

    PangoLayout *layout = make_layout(cr, text, font_family, font_size,
                                       PANGO_WEIGHT_NORMAL, max_width);
    pango_cairo_show_layout(cr, layout);

    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    g_object_unref(layout);
    cairo_restore(cr);
    return h;
}

int kd_draw_text_bold(cairo_t *cr, const char *text, double x, double y,
                       const char *font_family, double font_size,
                       kd_color_t color, double max_width)
{
    cairo_save(cr);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_move_to(cr, x, y);

    PangoLayout *layout = make_layout(cr, text, font_family, font_size,
                                       PANGO_WEIGHT_BOLD, max_width);
    pango_cairo_show_layout(cr, layout);

    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    g_object_unref(layout);
    cairo_restore(cr);
    return h;
}

int kd_draw_mono_text(cairo_t *cr, const char *text, double x, double y,
                       double font_size, kd_color_t color, double max_width)
{
    return kd_draw_text(cr, text, x, y, KD_FONT_MONO, font_size,
                         color, max_width);
}

void kd_measure_text(cairo_t *cr, const char *text,
                      const char *font_family, double font_size,
                      int *out_width, int *out_height)
{
    PangoLayout *layout = make_layout(cr, text, font_family, font_size,
                                       PANGO_WEIGHT_NORMAL, 0);
    pango_layout_get_pixel_size(layout, out_width, out_height);
    g_object_unref(layout);
}

int kd_draw_text_centered(cairo_t *cr, const char *text,
                            double x, double y, double width,
                            const char *font_family, double font_size,
                            kd_color_t color)
{
    int tw, th;
    kd_measure_text(cr, text, font_family, font_size, &tw, &th);
    double cx = x + (width - tw) / 2.0;
    return kd_draw_text(cr, text, cx, y, font_family, font_size, color, 0);
}

/* ---- Shapes ------------------------------------------------------------- */

void kd_fill_circle(cairo_t *cr, double cx, double cy,
                     double radius, kd_color_t color)
{
    cairo_save(cr);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_restore(cr);
}

void kd_fill_gradient_v(cairo_t *cr, double x, double y,
                         double w, double h,
                         kd_color_t top, kd_color_t bottom)
{
    cairo_save(cr);
    cairo_pattern_t *pat = cairo_pattern_create_linear(x, y, x, y + h);
    cairo_pattern_add_color_stop_rgba(pat, 0, top.r, top.g, top.b, top.a);
    cairo_pattern_add_color_stop_rgba(pat, 1, bottom.r, bottom.g, bottom.b, bottom.a);
    cairo_rectangle(cr, x, y, w, h);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);
    cairo_restore(cr);
}

void kd_draw_bar(cairo_t *cr, double x, double y, double w, double h,
                  double fill_pct, kd_color_t bg, kd_color_t fg)
{
    if (fill_pct < 0.0) fill_pct = 0.0;
    if (fill_pct > 1.0) fill_pct = 1.0;

    /* Background bar. */
    kd_fill_rounded_rect(cr, x, y, w, h, h / 2.0, bg);

    /* Fill bar. */
    if (fill_pct > 0.0) {
        double fw = w * fill_pct;
        if (fw < h) fw = h; /* minimum visible */
        kd_fill_rounded_rect(cr, x, y, fw, h, h / 2.0, fg);
    }
}
