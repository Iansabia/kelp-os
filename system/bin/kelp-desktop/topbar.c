/*
 * kelp-desktop :: topbar.c
 * Top bar: Kelp logo, AI status indicator, clock.
 *
 * SPDX-License-Identifier: MIT
 */

#include "topbar.h"
#include "desktop.h"
#include "render.h"
#include "theme.h"

#include <stdio.h>
#include <time.h>

void kd_topbar_draw(kd_desktop_t *d, cairo_t *cr)
{
    double w = (double)d->screen_w;

    /* Background. */
    kd_fill_rect(cr, 0, 0, w, KD_TOPBAR_HEIGHT, KD_BG_SECONDARY);

    /* Bottom border. */
    kd_draw_hline(cr, 0, KD_TOPBAR_HEIGHT - 1, w, KD_BORDER);

    /* Kelp logo dot + text. */
    kd_fill_circle(cr, 16, KD_TOPBAR_HEIGHT / 2.0, 5, KD_ACCENT_GREEN);

    kd_draw_text_bold(cr, "KELP OS", 28, 9,
                       KD_FONT_FAMILY, KD_FONT_SIZE_NORMAL,
                       KD_TEXT_PRIMARY, 0);

    /* AI status indicator (center-right). */
    {
        const char *status_text = d->gateway_connected
            ? "AI Active" : "AI Offline";
        kd_color_t status_color = d->gateway_connected
            ? KD_ACCENT_GREEN : KD_TEXT_DIM;

        int tw, th;
        kd_measure_text(cr, status_text, KD_FONT_FAMILY,
                         KD_FONT_SIZE_SMALL, &tw, &th);

        double sx = w - 80 - tw;
        kd_fill_circle(cr, sx - 8, KD_TOPBAR_HEIGHT / 2.0, 3, status_color);
        kd_draw_text(cr, status_text, sx, 11,
                      KD_FONT_FAMILY, KD_FONT_SIZE_SMALL,
                      status_color, 0);
    }

    /* Clock (right). */
    {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char timebuf[16];
        snprintf(timebuf, sizeof(timebuf), "%02d:%02d", tm->tm_hour, tm->tm_min);

        int tw, th;
        kd_measure_text(cr, timebuf, KD_FONT_FAMILY,
                         KD_FONT_SIZE_NORMAL, &tw, &th);
        kd_draw_text(cr, timebuf, w - tw - 16, 9,
                      KD_FONT_FAMILY, KD_FONT_SIZE_NORMAL,
                      KD_TEXT_SECONDARY, 0);
    }
}
