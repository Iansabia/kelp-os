/*
 * kelp-desktop :: desktop.c
 * Desktop state machine, panel management, z-order, focus.
 *
 * SPDX-License-Identifier: MIT
 */

#include "desktop.h"
#include "render.h"
#include "chat.h"
#include "terminal.h"
#include "monitor.h"
#include "files.h"
#include "topbar.h"
#include "dock.h"
#include "cursor.h"

#include <string.h>

/* ---- Panel titles ------------------------------------------------------- */

static const char *panel_titles[KD_PANEL_COUNT] = {
    [KD_PANEL_CHAT]     = "AI Chat",
    [KD_PANEL_TERMINAL] = "Terminal",
    [KD_PANEL_MONITOR]  = "System Monitor",
    [KD_PANEL_FILES]    = "Files",
};

/* ---- Init --------------------------------------------------------------- */

void kd_desktop_init(kd_desktop_t *d)
{
    memset(d->panels, 0, sizeof(d->panels));
    d->focus_panel = -1;
    d->panel_z_count = 0;

    for (int i = 0; i < KD_PANEL_COUNT; i++) {
        d->panels[i].type    = (kd_panel_type_t)i;
        d->panels[i].visible = false;
        d->panels[i].focused = false;
        d->panels[i].opacity = 0.0;
        d->panels[i].title   = panel_titles[i];
    }

    /* Boot animation: fade from black. */
    kd_anim_start(&d->boot_anim, 0.0, 1.0, 1200, KD_EASE_OUT_EXPO, kd_time_ms());
    d->boot_done = false;

    /* Open chat panel by default. */
    kd_desktop_open_panel(d, KD_PANEL_CHAT);
}

/* ---- Content area ------------------------------------------------------- */

void kd_desktop_content_area(kd_desktop_t *d,
                              double *x, double *y,
                              double *w, double *h)
{
    *x = KD_DOCK_WIDTH;
    *y = KD_TOPBAR_HEIGHT;
    *w = d->screen_w - KD_DOCK_WIDTH;
    *h = d->screen_h - KD_TOPBAR_HEIGHT;
}

/* ---- Panel management --------------------------------------------------- */

void kd_desktop_layout(kd_desktop_t *d)
{
    double cx, cy, cw, ch;
    kd_desktop_content_area(d, &cx, &cy, &cw, &ch);

    /* Count visible panels. */
    int vis_count = 0;
    for (int i = 0; i < KD_PANEL_COUNT; i++) {
        if (d->panels[i].visible)
            vis_count++;
    }
    if (vis_count == 0) return;

    /* Simple tiling: split content area evenly among visible panels. */
    double margin = KD_PANEL_MARGIN;
    double avail_w = cw - margin * 2;
    double avail_h = ch - margin * 2;

    if (vis_count == 1) {
        /* Single panel: fill content area. */
        for (int i = 0; i < KD_PANEL_COUNT; i++) {
            if (d->panels[i].visible) {
                d->panels[i].x = cx + margin;
                d->panels[i].y = cy + margin;
                d->panels[i].w = avail_w;
                d->panels[i].h = avail_h;
            }
        }
    } else if (vis_count == 2) {
        /* Two panels: side by side. */
        double pw = (avail_w - margin) / 2.0;
        int col = 0;
        for (int i = 0; i < KD_PANEL_COUNT; i++) {
            if (d->panels[i].visible) {
                d->panels[i].x = cx + margin + col * (pw + margin);
                d->panels[i].y = cy + margin;
                d->panels[i].w = pw;
                d->panels[i].h = avail_h;
                col++;
            }
        }
    } else {
        /* 3-4 panels: 2x2 grid. */
        double pw = (avail_w - margin) / 2.0;
        double ph = (avail_h - margin) / 2.0;
        int idx = 0;
        for (int i = 0; i < KD_PANEL_COUNT; i++) {
            if (d->panels[i].visible) {
                int row = idx / 2;
                int col = idx % 2;
                d->panels[i].x = cx + margin + col * (pw + margin);
                d->panels[i].y = cy + margin + row * (ph + margin);
                d->panels[i].w = pw;
                d->panels[i].h = ph;
                idx++;
            }
        }
    }
}

void kd_desktop_open_panel(kd_desktop_t *d, kd_panel_type_t type)
{
    if (type < 0 || type >= KD_PANEL_COUNT) return;
    kd_panel_t *p = &d->panels[type];

    if (p->visible) {
        kd_desktop_focus_panel(d, type);
        return;
    }

    p->visible = true;
    uint32_t now = kd_time_ms();
    kd_anim_start(&p->anim_opacity, 0.0, 1.0, KD_ANIM_DURATION_MS,
                   KD_EASE_OUT_CUBIC, now);

    kd_desktop_layout(d);
    kd_desktop_focus_panel(d, type);
    d->needs_redraw = true;
}

void kd_desktop_close_panel(kd_desktop_t *d, kd_panel_type_t type)
{
    if (type < 0 || type >= KD_PANEL_COUNT) return;
    kd_panel_t *p = &d->panels[type];

    p->visible = false;
    p->focused = false;
    p->opacity = 0.0;

    if (d->focus_panel == type) {
        d->focus_panel = -1;
        /* Focus the next visible panel. */
        for (int i = 0; i < KD_PANEL_COUNT; i++) {
            if (d->panels[i].visible) {
                kd_desktop_focus_panel(d, (kd_panel_type_t)i);
                break;
            }
        }
    }

    kd_desktop_layout(d);
    d->needs_redraw = true;
}

void kd_desktop_toggle_panel(kd_desktop_t *d, kd_panel_type_t type)
{
    if (type < 0 || type >= KD_PANEL_COUNT) return;
    if (d->panels[type].visible)
        kd_desktop_close_panel(d, type);
    else
        kd_desktop_open_panel(d, type);
}

void kd_desktop_focus_panel(kd_desktop_t *d, kd_panel_type_t type)
{
    if (type < 0 || type >= KD_PANEL_COUNT) return;
    if (!d->panels[type].visible) return;

    for (int i = 0; i < KD_PANEL_COUNT; i++)
        d->panels[i].focused = false;

    d->panels[type].focused = true;
    d->focus_panel = type;
    d->needs_redraw = true;
}

/* ---- Event handling ----------------------------------------------------- */

void kd_desktop_handle_event(kd_desktop_t *d, SDL_Event *event)
{
    switch (event->type) {
    case SDL_QUIT:
        d->running = false;
        break;

    case SDL_MOUSEMOTION:
        d->mouse_x = event->motion.x;
        d->mouse_y = event->motion.y;
        d->needs_redraw = true;
        break;

    case SDL_MOUSEBUTTONDOWN:
        d->mouse_down = true;
        d->mouse_x = event->button.x;
        d->mouse_y = event->button.y;

        /* Check dock clicks. */
        if (event->button.x < KD_DOCK_WIDTH &&
            event->button.y > KD_TOPBAR_HEIGHT) {
            kd_dock_handle_click(d, event->button.x, event->button.y);
            break;
        }

        /* Check panel clicks — focus the clicked panel. */
        for (int i = KD_PANEL_COUNT - 1; i >= 0; i--) {
            kd_panel_t *p = &d->panels[i];
            if (!p->visible) continue;
            if (event->button.x >= p->x && event->button.x <= p->x + p->w &&
                event->button.y >= p->y && event->button.y <= p->y + p->h) {
                kd_desktop_focus_panel(d, (kd_panel_type_t)i);

                /* Forward click to panel. */
                double px = event->button.x - p->x;
                double py = event->button.y - p->y;
                switch (i) {
                case KD_PANEL_CHAT:
                    kd_chat_handle_click(d, px, py);
                    break;
                case KD_PANEL_TERMINAL:
                    kd_terminal_handle_click(d, px, py);
                    break;
                case KD_PANEL_FILES:
                    kd_files_handle_click(d, px, py);
                    break;
                default:
                    break;
                }
                break;
            }
        }
        break;

    case SDL_MOUSEBUTTONUP:
        d->mouse_down = false;
        break;

    case SDL_KEYDOWN:
        /* Forward keyboard to focused panel. */
        if (d->focus_panel >= 0) {
            switch (d->focus_panel) {
            case KD_PANEL_CHAT:
                kd_chat_handle_key(d, &event->key);
                break;
            case KD_PANEL_TERMINAL:
                kd_terminal_handle_key(d, &event->key);
                break;
            case KD_PANEL_FILES:
                kd_files_handle_key(d, &event->key);
                break;
            default:
                break;
            }
        }
        break;

    case SDL_TEXTINPUT:
        if (d->focus_panel >= 0) {
            switch (d->focus_panel) {
            case KD_PANEL_CHAT:
                kd_chat_handle_text(d, event->text.text);
                break;
            case KD_PANEL_TERMINAL:
                kd_terminal_handle_text(d, event->text.text);
                break;
            default:
                break;
            }
        }
        break;

    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_RESIZED ||
            event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            d->screen_w = event->window.data1;
            d->screen_h = event->window.data2;
            kd_desktop_layout(d);
            d->needs_redraw = true;
        }
        break;

    default:
        break;
    }
}

/* ---- Update ------------------------------------------------------------- */

void kd_desktop_update(kd_desktop_t *d, uint32_t now_ms)
{
    /* Boot animation. */
    if (!d->boot_done) {
        kd_anim_update(&d->boot_anim, now_ms);
        if (d->boot_anim.finished)
            d->boot_done = true;
        d->needs_redraw = true;
    }

    /* Panel animations. */
    for (int i = 0; i < KD_PANEL_COUNT; i++) {
        kd_panel_t *p = &d->panels[i];
        if (p->anim_opacity.active) {
            p->opacity = kd_anim_update(&p->anim_opacity, now_ms);
            d->needs_redraw = true;
        } else if (p->visible && p->opacity < 1.0) {
            p->opacity = 1.0;
        }
    }

    /* Update panel contents. */
    kd_chat_update(d, now_ms);
    kd_terminal_update(d, now_ms);
    kd_monitor_update(d, now_ms);
}

/* ---- Drawing ------------------------------------------------------------ */

static void draw_panel_frame(cairo_t *cr, kd_panel_t *p)
{
    if (p->opacity < 0.01) return;

    cairo_save(cr);
    if (p->opacity < 1.0)
        cairo_push_group(cr);

    /* Shadow. */
    kd_draw_shadow(cr, p->x, p->y, p->w, p->h,
                    KD_PANEL_CORNER, KD_PANEL_SHADOW, KD_SHADOW);

    /* Panel background. */
    kd_fill_rounded_rect(cr, p->x, p->y, p->w, p->h,
                          KD_PANEL_CORNER, KD_BG_PANEL);

    /* Title bar. */
    kd_fill_rounded_rect(cr, p->x, p->y, p->w, KD_PANEL_TITLEBAR,
                          KD_PANEL_CORNER, KD_BG_ELEVATED);
    /* Fix bottom corners of title bar. */
    kd_fill_rect(cr, p->x, p->y + KD_PANEL_TITLEBAR - KD_PANEL_CORNER,
                  p->w, KD_PANEL_CORNER, KD_BG_ELEVATED);

    /* Title text. */
    kd_draw_text_bold(cr, p->title,
                       p->x + KD_PANEL_PADDING, p->y + 8,
                       KD_FONT_FAMILY, KD_FONT_SIZE_NORMAL,
                       p->focused ? KD_TEXT_PRIMARY : KD_TEXT_SECONDARY, 0);

    /* Focus indicator. */
    if (p->focused) {
        kd_fill_rect(cr, p->x, p->y, 3, KD_PANEL_TITLEBAR, KD_ACCENT_GREEN);
    }

    /* Border. */
    kd_draw_border(cr, p->x, p->y, p->w, p->h, KD_PANEL_CORNER,
                    p->focused ? KD_ACCENT_GREEN_DIM : KD_BORDER);

    /* Title bar separator. */
    kd_draw_hline(cr, p->x + 1, p->y + KD_PANEL_TITLEBAR,
                   p->w - 2, KD_BORDER);

    if (p->opacity < 1.0) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, p->opacity);
    }

    cairo_restore(cr);
}

void kd_desktop_draw_panels(kd_desktop_t *d, cairo_t *cr)
{
    /* Draw panels in order (non-focused first, focused last). */
    for (int i = 0; i < KD_PANEL_COUNT; i++) {
        kd_panel_t *p = &d->panels[i];
        if (!p->visible || p->focused) continue;
        draw_panel_frame(cr, p);

        /* Draw panel content. */
        cairo_save(cr);
        cairo_rectangle(cr, p->x + 1, p->y + KD_PANEL_TITLEBAR + 1,
                         p->w - 2, p->h - KD_PANEL_TITLEBAR - 2);
        cairo_clip(cr);
        double content_x = p->x;
        double content_y = p->y + KD_PANEL_TITLEBAR;
        double content_w = p->w;
        double content_h = p->h - KD_PANEL_TITLEBAR;

        switch (p->type) {
        case KD_PANEL_CHAT:
            kd_chat_draw(d, cr, content_x, content_y, content_w, content_h);
            break;
        case KD_PANEL_TERMINAL:
            kd_terminal_draw(d, cr, content_x, content_y, content_w, content_h);
            break;
        case KD_PANEL_MONITOR:
            kd_monitor_draw(d, cr, content_x, content_y, content_w, content_h);
            break;
        case KD_PANEL_FILES:
            kd_files_draw(d, cr, content_x, content_y, content_w, content_h);
            break;
        default:
            break;
        }
        cairo_restore(cr);
    }

    /* Draw focused panel last (on top). */
    if (d->focus_panel >= 0) {
        kd_panel_t *p = &d->panels[d->focus_panel];
        if (p->visible) {
            draw_panel_frame(cr, p);

            cairo_save(cr);
            cairo_rectangle(cr, p->x + 1, p->y + KD_PANEL_TITLEBAR + 1,
                             p->w - 2, p->h - KD_PANEL_TITLEBAR - 2);
            cairo_clip(cr);
            double content_x = p->x;
            double content_y = p->y + KD_PANEL_TITLEBAR;
            double content_w = p->w;
            double content_h = p->h - KD_PANEL_TITLEBAR;

            switch (p->type) {
            case KD_PANEL_CHAT:
                kd_chat_draw(d, cr, content_x, content_y, content_w, content_h);
                break;
            case KD_PANEL_TERMINAL:
                kd_terminal_draw(d, cr, content_x, content_y, content_w, content_h);
                break;
            case KD_PANEL_MONITOR:
                kd_monitor_draw(d, cr, content_x, content_y, content_w, content_h);
                break;
            case KD_PANEL_FILES:
                kd_files_draw(d, cr, content_x, content_y, content_w, content_h);
                break;
            default:
                break;
            }
            cairo_restore(cr);
        }
    }
}
