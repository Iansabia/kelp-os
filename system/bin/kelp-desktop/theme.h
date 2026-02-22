/*
 * kelp-desktop :: theme.h
 * Color palette, font sizes, spacing, corner radii constants.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_THEME_H
#define KELP_DESKTOP_THEME_H

#include <stdint.h>

/* ---- RGBA color --------------------------------------------------------- */

typedef struct {
    double r, g, b, a;
} kd_color_t;

/* ---- Theme constants ---------------------------------------------------- */

/* Background */
#define KD_BG_PRIMARY       (kd_color_t){0.051, 0.067, 0.090, 1.0}  /* #0d1117 */
#define KD_BG_SECONDARY     (kd_color_t){0.082, 0.098, 0.129, 1.0}  /* #151921 */
#define KD_BG_SURFACE       (kd_color_t){0.098, 0.118, 0.153, 1.0}  /* #191e27 */
#define KD_BG_ELEVATED      (kd_color_t){0.125, 0.149, 0.188, 1.0}  /* #202630 */
#define KD_BG_PANEL         (kd_color_t){0.071, 0.090, 0.118, 1.0}  /* #12171e */

/* Accent */
#define KD_ACCENT_GREEN     (kd_color_t){0.0, 0.784, 0.325, 1.0}    /* #00c853 */
#define KD_ACCENT_GREEN_DIM (kd_color_t){0.0, 0.784, 0.325, 0.3}
#define KD_ACCENT_BLUE      (kd_color_t){0.345, 0.639, 1.0, 1.0}    /* #58a3ff */

/* Text */
#define KD_TEXT_PRIMARY      (kd_color_t){0.898, 0.914, 0.937, 1.0}  /* #e5e9ef */
#define KD_TEXT_SECONDARY    (kd_color_t){0.533, 0.580, 0.643, 1.0}  /* #8894a4 */
#define KD_TEXT_DIM          (kd_color_t){0.369, 0.412, 0.467, 1.0}  /* #5e6977 */
#define KD_TEXT_CODE         (kd_color_t){0.690, 0.820, 0.996, 1.0}  /* #b0d1fe */

/* Status */
#define KD_STATUS_ERROR     (kd_color_t){1.0, 0.329, 0.329, 1.0}    /* #ff5454 */
#define KD_STATUS_WARNING   (kd_color_t){1.0, 0.784, 0.0, 1.0}      /* #ffc800 */
#define KD_STATUS_OK        (kd_color_t){0.0, 0.784, 0.325, 1.0}    /* #00c853 */

/* Borders & shadows */
#define KD_BORDER           (kd_color_t){0.176, 0.208, 0.259, 1.0}  /* #2d3542 */
#define KD_SHADOW           (kd_color_t){0.0, 0.0, 0.0, 0.4}

/* ---- Dimensions --------------------------------------------------------- */

#define KD_TOPBAR_HEIGHT     36
#define KD_DOCK_WIDTH        52
#define KD_DOCK_ICON_SIZE    28
#define KD_DOCK_PADDING      12

#define KD_PANEL_MARGIN      12
#define KD_PANEL_PADDING     16
#define KD_PANEL_CORNER      8.0
#define KD_PANEL_SHADOW      6.0
#define KD_PANEL_TITLEBAR    32

#define KD_SCROLLBAR_WIDTH   6

/* ---- Font sizes --------------------------------------------------------- */

#define KD_FONT_FAMILY       "DejaVu Sans"
#define KD_FONT_MONO         "DejaVu Sans Mono"

#define KD_FONT_SIZE_SMALL   11.0
#define KD_FONT_SIZE_NORMAL  13.0
#define KD_FONT_SIZE_LARGE   15.0
#define KD_FONT_SIZE_TITLE   18.0
#define KD_FONT_SIZE_MONO    12.0

/* ---- Animation ---------------------------------------------------------- */

#define KD_ANIM_DURATION_MS   200
#define KD_ANIM_FAST_MS       120
#define KD_ANIM_SLOW_MS       400

/* ---- API ---------------------------------------------------------------- */

void kd_theme_apply_color(void *cr, kd_color_t color);

#endif /* KELP_DESKTOP_THEME_H */
