/*
 * kelp-desktop :: animation.h
 * Easing functions, transition state, lerp utilities.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_ANIMATION_H
#define KELP_DESKTOP_ANIMATION_H

#include <stdbool.h>
#include <stdint.h>

/* ---- Easing types ------------------------------------------------------- */

typedef enum {
    KD_EASE_LINEAR,
    KD_EASE_IN_QUAD,
    KD_EASE_OUT_QUAD,
    KD_EASE_IN_OUT_QUAD,
    KD_EASE_OUT_CUBIC,
    KD_EASE_OUT_EXPO,
} kd_ease_type_t;

/* ---- Animation state ---------------------------------------------------- */

typedef struct {
    double   start;
    double   end;
    double   current;
    uint32_t start_ms;
    uint32_t duration_ms;
    kd_ease_type_t ease;
    bool     active;
    bool     finished;
} kd_anim_t;

/* ---- API ---------------------------------------------------------------- */

/** Start a new animation from start to end. */
void kd_anim_start(kd_anim_t *a, double start, double end,
                    uint32_t duration_ms, kd_ease_type_t ease,
                    uint32_t now_ms);

/** Update the animation. Returns current value. */
double kd_anim_update(kd_anim_t *a, uint32_t now_ms);

/** Linear interpolation. */
double kd_lerp(double a, double b, double t);

/** Apply easing function. */
double kd_ease(kd_ease_type_t type, double t);

/** Get current time in milliseconds (SDL tick). */
uint32_t kd_time_ms(void);

#endif /* KELP_DESKTOP_ANIMATION_H */
