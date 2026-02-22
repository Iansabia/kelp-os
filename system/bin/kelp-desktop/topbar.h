/*
 * kelp-desktop :: topbar.h
 * Top bar: Kelp logo, AI status indicator, clock.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_TOPBAR_H
#define KELP_DESKTOP_TOPBAR_H

#include <cairo/cairo.h>

struct kd_desktop;

/** Draw the top bar. */
void kd_topbar_draw(struct kd_desktop *d, cairo_t *cr);

#endif /* KELP_DESKTOP_TOPBAR_H */
