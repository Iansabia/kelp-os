/*
 * kelp-desktop :: cursor.c
 * Hardware + AI ghost cursor rendering.
 *
 * SPDX-License-Identifier: MIT
 */

#include "cursor.h"
#include "desktop.h"
#include "render.h"
#include "theme.h"
#include "animation.h"

#include <math.h>
#include <string.h>

/* ---- State -------------------------------------------------------------- */

static kd_ai_cursor_t g_ai_cursor;

/* ---- Init/Shutdown ------------------------------------------------------ */

void kd_cursor_init(kd_desktop_t *d)
{
    (void)d;
    memset(&g_ai_cursor, 0, sizeof(g_ai_cursor));
}

void kd_cursor_shutdown(kd_desktop_t *d)
{
    (void)d;
}

/* ---- AI cursor control -------------------------------------------------- */

kd_ai_cursor_t *kd_cursor_get_ai(void)
{
    return &g_ai_cursor;
}

void kd_cursor_move_to(double x, double y)
{
    g_ai_cursor.target_x = x;
    g_ai_cursor.target_y = y;
    g_ai_cursor.active = true;
}

void kd_cursor_click(void)
{
    g_ai_cursor.clicking = true;
    g_ai_cursor.click_anim = 0.0;
}

/* ---- Drawing ------------------------------------------------------------ */

static void draw_hardware_cursor(cairo_t *cr, int mx, int my)
{
    /* Simple arrow cursor. */
    cairo_save(cr);
    cairo_translate(cr, mx, my);

    /* Shadow. */
    cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
    cairo_move_to(cr, 1, 1);
    cairo_line_to(cr, 1, 19);
    cairo_line_to(cr, 7, 14);
    cairo_line_to(cr, 12, 19);
    cairo_line_to(cr, 14, 17);
    cairo_line_to(cr, 9, 12);
    cairo_line_to(cr, 15, 12);
    cairo_close_path(cr);
    cairo_fill(cr);

    /* White fill. */
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, 0, 18);
    cairo_line_to(cr, 6, 13);
    cairo_line_to(cr, 11, 18);
    cairo_line_to(cr, 13, 16);
    cairo_line_to(cr, 8, 11);
    cairo_line_to(cr, 14, 11);
    cairo_close_path(cr);
    cairo_fill(cr);

    /* Black outline. */
    cairo_set_source_rgba(cr, 0, 0, 0, 1);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, 0, 18);
    cairo_line_to(cr, 6, 13);
    cairo_line_to(cr, 11, 18);
    cairo_line_to(cr, 13, 16);
    cairo_line_to(cr, 8, 11);
    cairo_line_to(cr, 14, 11);
    cairo_close_path(cr);
    cairo_stroke(cr);

    cairo_restore(cr);
}

static void draw_ai_cursor(cairo_t *cr)
{
    if (!g_ai_cursor.active) return;

    /* Smoothly interpolate toward target. */
    double speed = 0.12;
    g_ai_cursor.current_x += (g_ai_cursor.target_x - g_ai_cursor.current_x) * speed;
    g_ai_cursor.current_y += (g_ai_cursor.target_y - g_ai_cursor.current_y) * speed;

    double dx = g_ai_cursor.target_x - g_ai_cursor.current_x;
    double dy = g_ai_cursor.target_y - g_ai_cursor.current_y;
    if (fabs(dx) < 1.0 && fabs(dy) < 1.0) {
        g_ai_cursor.current_x = g_ai_cursor.target_x;
        g_ai_cursor.current_y = g_ai_cursor.target_y;
        if (!g_ai_cursor.clicking)
            g_ai_cursor.active = false;
    }

    double x = g_ai_cursor.current_x;
    double y = g_ai_cursor.current_y;

    /* Glow ring. */
    kd_color_t glow = KD_ACCENT_GREEN;
    glow.a = 0.15;
    kd_fill_circle(cr, x, y, 16, glow);

    /* AI cursor (green dot with ring). */
    kd_color_t ring = KD_ACCENT_GREEN;
    ring.a = 0.5;
    cairo_save(cr);
    cairo_set_source_rgba(cr, ring.r, ring.g, ring.b, ring.a);
    cairo_set_line_width(cr, 2.0);
    cairo_arc(cr, x, y, 8, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_restore(cr);

    kd_fill_circle(cr, x, y, 4, KD_ACCENT_GREEN);

    /* Click ripple animation. */
    if (g_ai_cursor.clicking) {
        g_ai_cursor.click_anim += 0.05;
        if (g_ai_cursor.click_anim >= 1.0) {
            g_ai_cursor.clicking = false;
            g_ai_cursor.click_anim = 0.0;
        } else {
            double r = 8.0 + g_ai_cursor.click_anim * 20.0;
            kd_color_t ripple = KD_ACCENT_GREEN;
            ripple.a = 0.4 * (1.0 - g_ai_cursor.click_anim);
            cairo_save(cr);
            cairo_set_source_rgba(cr, ripple.r, ripple.g, ripple.b, ripple.a);
            cairo_set_line_width(cr, 2.0);
            cairo_arc(cr, x, y, r, 0, 2 * M_PI);
            cairo_stroke(cr);
            cairo_restore(cr);
        }
    }
}

void kd_cursor_draw(kd_desktop_t *d, cairo_t *cr)
{
    draw_ai_cursor(cr);
    draw_hardware_cursor(cr, d->mouse_x, d->mouse_y);
}
