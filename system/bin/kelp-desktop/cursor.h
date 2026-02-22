/*
 * kelp-desktop :: cursor.h
 * Hardware + AI ghost cursor rendering.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_CURSOR_H
#define KELP_DESKTOP_CURSOR_H

#include <cairo/cairo.h>
#include <stdbool.h>

struct kd_desktop;

/* ---- AI cursor state ---------------------------------------------------- */

typedef struct {
    double target_x, target_y;
    double current_x, current_y;
    bool   active;           /* true when AI is moving cursor */
    bool   clicking;         /* true during click animation */
    double click_anim;       /* 0..1 click ripple */
} kd_ai_cursor_t;

/* ---- API ---------------------------------------------------------------- */

void kd_cursor_init(struct kd_desktop *d);
void kd_cursor_shutdown(struct kd_desktop *d);
void kd_cursor_draw(struct kd_desktop *d, cairo_t *cr);

/** Move the AI cursor to a position (animated). */
void kd_cursor_move_to(double x, double y);

/** Trigger AI click animation at current position. */
void kd_cursor_click(void);

/** Get AI cursor state. */
kd_ai_cursor_t *kd_cursor_get_ai(void);

#endif /* KELP_DESKTOP_CURSOR_H */
