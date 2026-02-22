/*
 * kelp-desktop :: dock.c
 * Left dock: panel launcher icons.
 *
 * SPDX-License-Identifier: MIT
 */

#include "dock.h"
#include "desktop.h"
#include "render.h"
#include "theme.h"

#include <string.h>

/* ---- Icon definitions --------------------------------------------------- */

typedef struct {
    kd_panel_type_t panel;
    const char     *label;      /* Short text icon (emoji-free, just letters) */
    const char     *tooltip;
} dock_icon_t;

static const dock_icon_t dock_icons[] = {
    { KD_PANEL_CHAT,     "Ai",  "AI Chat" },
    { KD_PANEL_TERMINAL, ">_",  "Terminal" },
    { KD_PANEL_MONITOR,  "Mo",  "System Monitor" },
    { KD_PANEL_FILES,    "Fi",  "Files" },
};

#define DOCK_ICON_COUNT (sizeof(dock_icons) / sizeof(dock_icons[0]))

static double dock_icon_y(int idx)
{
    return KD_TOPBAR_HEIGHT + KD_DOCK_PADDING + idx * (KD_DOCK_ICON_SIZE + KD_DOCK_PADDING);
}

/* ---- Drawing ------------------------------------------------------------ */

void kd_dock_draw(kd_desktop_t *d, cairo_t *cr)
{
    double h = (double)d->screen_h - KD_TOPBAR_HEIGHT;

    /* Background. */
    kd_fill_rect(cr, 0, KD_TOPBAR_HEIGHT, KD_DOCK_WIDTH, h, KD_BG_SECONDARY);

    /* Right border. */
    kd_draw_vline(cr, KD_DOCK_WIDTH - 1, KD_TOPBAR_HEIGHT, h, KD_BORDER);

    /* Draw icons. */
    for (int i = 0; i < (int)DOCK_ICON_COUNT; i++) {
        double iy = dock_icon_y(i);
        double ix = (KD_DOCK_WIDTH - KD_DOCK_ICON_SIZE) / 2.0;

        bool is_active = d->panels[dock_icons[i].panel].visible;
        bool is_hovered = (d->mouse_x < KD_DOCK_WIDTH &&
                           d->mouse_y >= (int)iy &&
                           d->mouse_y < (int)(iy + KD_DOCK_ICON_SIZE));

        /* Icon background. */
        kd_color_t icon_bg = is_active ? KD_BG_ELEVATED
                            : is_hovered ? KD_BG_SURFACE
                            : KD_BG_SECONDARY;
        kd_fill_rounded_rect(cr, ix, iy, KD_DOCK_ICON_SIZE,
                              KD_DOCK_ICON_SIZE, 6.0, icon_bg);

        /* Active indicator (green left bar). */
        if (is_active) {
            kd_fill_rounded_rect(cr, 2, iy + 4, 3,
                                  KD_DOCK_ICON_SIZE - 8, 1.5, KD_ACCENT_GREEN);
        }

        /* Icon text. */
        kd_color_t text_color = is_active ? KD_ACCENT_GREEN
                                : is_hovered ? KD_TEXT_PRIMARY
                                : KD_TEXT_SECONDARY;
        kd_draw_text_centered(cr, dock_icons[i].label,
                               ix, iy + 6, KD_DOCK_ICON_SIZE,
                               KD_FONT_MONO, KD_FONT_SIZE_SMALL, text_color);

        /* Tooltip on hover. */
        if (is_hovered) {
            double tx = KD_DOCK_WIDTH + 4;
            double ty = iy + 4;
            int tw, th;
            kd_measure_text(cr, dock_icons[i].tooltip,
                             KD_FONT_FAMILY, KD_FONT_SIZE_SMALL, &tw, &th);
            kd_fill_rounded_rect(cr, tx, ty, tw + 12, th + 8,
                                  4.0, KD_BG_ELEVATED);
            kd_draw_border(cr, tx, ty, tw + 12, th + 8, 4.0, KD_BORDER);
            kd_draw_text(cr, dock_icons[i].tooltip, tx + 6, ty + 4,
                          KD_FONT_FAMILY, KD_FONT_SIZE_SMALL,
                          KD_TEXT_PRIMARY, 0);
        }
    }
}

/* ---- Click handling ----------------------------------------------------- */

void kd_dock_handle_click(kd_desktop_t *d, int x, int y)
{
    (void)x;
    for (int i = 0; i < (int)DOCK_ICON_COUNT; i++) {
        double iy = dock_icon_y(i);
        if (y >= (int)iy && y < (int)(iy + KD_DOCK_ICON_SIZE)) {
            kd_desktop_toggle_panel(d, dock_icons[i].panel);
            return;
        }
    }
}
