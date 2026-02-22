/*
 * kelp-desktop :: render.h
 * Cairo drawing primitives: rounded rect, shadow, gradient, text.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_RENDER_H
#define KELP_DESKTOP_RENDER_H

#include "theme.h"
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <stdbool.h>

/* ---- Drawing primitives ------------------------------------------------- */

/** Draw a rounded rectangle path (does not stroke/fill). */
void kd_rounded_rect(cairo_t *cr, double x, double y,
                      double w, double h, double radius);

/** Fill a rounded rectangle with color. */
void kd_fill_rounded_rect(cairo_t *cr, double x, double y,
                            double w, double h, double radius,
                            kd_color_t color);

/** Draw a drop shadow behind a rounded rectangle. */
void kd_draw_shadow(cairo_t *cr, double x, double y,
                     double w, double h, double radius,
                     double blur, kd_color_t color);

/** Draw a 1px border on a rounded rectangle. */
void kd_draw_border(cairo_t *cr, double x, double y,
                     double w, double h, double radius,
                     kd_color_t color);

/** Fill a rectangle (no rounding). */
void kd_fill_rect(cairo_t *cr, double x, double y,
                   double w, double h, kd_color_t color);

/** Draw horizontal line. */
void kd_draw_hline(cairo_t *cr, double x, double y,
                    double w, kd_color_t color);

/** Draw vertical line. */
void kd_draw_vline(cairo_t *cr, double x, double y,
                    double h, kd_color_t color);

/* ---- Text drawing ------------------------------------------------------- */

/** Draw text at position (left-aligned). Returns the height used. */
int kd_draw_text(cairo_t *cr, const char *text, double x, double y,
                  const char *font_family, double font_size,
                  kd_color_t color, double max_width);

/** Draw text with Pango bold weight. */
int kd_draw_text_bold(cairo_t *cr, const char *text, double x, double y,
                       const char *font_family, double font_size,
                       kd_color_t color, double max_width);

/** Draw monospace text (for terminal/code). Returns height. */
int kd_draw_mono_text(cairo_t *cr, const char *text, double x, double y,
                       double font_size, kd_color_t color, double max_width);

/** Measure text width/height. */
void kd_measure_text(cairo_t *cr, const char *text,
                      const char *font_family, double font_size,
                      int *out_width, int *out_height);

/** Draw text centered horizontally within given width. */
int kd_draw_text_centered(cairo_t *cr, const char *text,
                            double x, double y, double width,
                            const char *font_family, double font_size,
                            kd_color_t color);

/** Draw a filled circle. */
void kd_fill_circle(cairo_t *cr, double cx, double cy,
                     double radius, kd_color_t color);

/** Draw a vertical gradient rect. */
void kd_fill_gradient_v(cairo_t *cr, double x, double y,
                         double w, double h,
                         kd_color_t top, kd_color_t bottom);

/* ---- Bar chart ---------------------------------------------------------- */

/** Draw a horizontal bar (for metrics). */
void kd_draw_bar(cairo_t *cr, double x, double y, double w, double h,
                  double fill_pct, kd_color_t bg, kd_color_t fg);

#endif /* KELP_DESKTOP_RENDER_H */
