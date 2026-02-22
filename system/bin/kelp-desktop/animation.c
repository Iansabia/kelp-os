/*
 * kelp-desktop :: animation.c
 * Easing functions, transition state, lerp utilities.
 *
 * SPDX-License-Identifier: MIT
 */

#include "animation.h"
#include <SDL2/SDL.h>
#include <math.h>

double kd_lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

double kd_ease(kd_ease_type_t type, double t)
{
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;

    switch (type) {
    case KD_EASE_LINEAR:
        return t;
    case KD_EASE_IN_QUAD:
        return t * t;
    case KD_EASE_OUT_QUAD:
        return t * (2.0 - t);
    case KD_EASE_IN_OUT_QUAD:
        return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
    case KD_EASE_OUT_CUBIC:
        { double f = t - 1.0; return f * f * f + 1.0; }
    case KD_EASE_OUT_EXPO:
        return 1.0 - pow(2.0, -10.0 * t);
    }
    return t;
}

void kd_anim_start(kd_anim_t *a, double start, double end,
                    uint32_t duration_ms, kd_ease_type_t ease,
                    uint32_t now_ms)
{
    a->start       = start;
    a->end         = end;
    a->current     = start;
    a->start_ms    = now_ms;
    a->duration_ms = duration_ms;
    a->ease        = ease;
    a->active      = true;
    a->finished    = false;
}

double kd_anim_update(kd_anim_t *a, uint32_t now_ms)
{
    if (!a->active || a->finished) {
        return a->current;
    }

    uint32_t elapsed = now_ms - a->start_ms;
    if (elapsed >= a->duration_ms) {
        a->current  = a->end;
        a->active   = false;
        a->finished = true;
        return a->current;
    }

    double t = (double)elapsed / (double)a->duration_ms;
    double eased = kd_ease(a->ease, t);
    a->current = kd_lerp(a->start, a->end, eased);
    return a->current;
}

uint32_t kd_time_ms(void)
{
    return SDL_GetTicks();
}
