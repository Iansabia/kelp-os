/*
 * kelp-desktop :: theme.c
 * Theme utility functions.
 *
 * SPDX-License-Identifier: MIT
 */

#include "theme.h"
#include <cairo/cairo.h>

void kd_theme_apply_color(void *cr, kd_color_t color)
{
    cairo_set_source_rgba((cairo_t *)cr, color.r, color.g, color.b, color.a);
}
