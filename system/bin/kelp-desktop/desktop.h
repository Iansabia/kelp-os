/*
 * kelp-desktop :: desktop.h
 * Desktop state machine, panel management, z-order, focus.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_DESKTOP_H
#define KELP_DESKTOP_DESKTOP_H

#include "theme.h"
#include "animation.h"

#include <cairo/cairo.h>
#include <SDL2/SDL.h>
#include <stdbool.h>

/* ---- Panel types -------------------------------------------------------- */

typedef enum {
    KD_PANEL_CHAT,
    KD_PANEL_TERMINAL,
    KD_PANEL_MONITOR,
    KD_PANEL_FILES,
    KD_PANEL_COUNT
} kd_panel_type_t;

/* ---- Panel state -------------------------------------------------------- */

typedef struct {
    kd_panel_type_t type;
    bool   visible;
    bool   focused;
    double x, y, w, h;              /* current position/size */
    kd_anim_t anim_x;               /* slide animation */
    kd_anim_t anim_opacity;          /* fade animation */
    double opacity;                  /* current opacity 0..1 */
    const char *title;
} kd_panel_t;

/* ---- Desktop state ------------------------------------------------------ */

typedef struct kd_desktop {
    /* SDL */
    SDL_Window    *window;
    SDL_Renderer  *renderer;
    SDL_Texture   *texture;

    /* Dimensions */
    int screen_w;
    int screen_h;

    /* Panels */
    kd_panel_t panels[KD_PANEL_COUNT];
    int        focus_panel;          /* index into panels[], or -1 */
    int        panel_z[KD_PANEL_COUNT]; /* z-order indices */
    int        panel_z_count;

    /* Mouse state */
    int mouse_x, mouse_y;
    bool mouse_down;

    /* Keyboard */
    bool text_input_active;

    /* Gateway connection */
    int gateway_fd;
    bool gateway_connected;

    /* Global state */
    bool running;
    bool needs_redraw;
    uint32_t frame_count;

    /* Boot animation */
    kd_anim_t boot_anim;
    bool boot_done;
} kd_desktop_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialize the desktop state (call after SDL is set up). */
void kd_desktop_init(kd_desktop_t *d);

/** Open a panel by type. */
void kd_desktop_open_panel(kd_desktop_t *d, kd_panel_type_t type);

/** Close a panel by type. */
void kd_desktop_close_panel(kd_desktop_t *d, kd_panel_type_t type);

/** Toggle a panel. */
void kd_desktop_toggle_panel(kd_desktop_t *d, kd_panel_type_t type);

/** Set focus to a panel. */
void kd_desktop_focus_panel(kd_desktop_t *d, kd_panel_type_t type);

/** Layout panels within the content area. */
void kd_desktop_layout(kd_desktop_t *d);

/** Process an SDL event. */
void kd_desktop_handle_event(kd_desktop_t *d, SDL_Event *event);

/** Draw all panels. */
void kd_desktop_draw_panels(kd_desktop_t *d, cairo_t *cr);

/** Update animations, metrics, etc. Called each frame. */
void kd_desktop_update(kd_desktop_t *d, uint32_t now_ms);

/** Get the content area (below topbar, right of dock). */
void kd_desktop_content_area(kd_desktop_t *d,
                              double *x, double *y,
                              double *w, double *h);

#endif /* KELP_DESKTOP_DESKTOP_H */
