/*
 * kelp-desktop :: dock.h
 * Left dock: panel launcher icons.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_DOCK_H
#define KELP_DESKTOP_DOCK_H

#include <cairo/cairo.h>

struct kd_desktop;

/** Draw the left dock. */
void kd_dock_draw(struct kd_desktop *d, cairo_t *cr);

/** Handle a click on the dock. */
void kd_dock_handle_click(struct kd_desktop *d, int x, int y);

#endif /* KELP_DESKTOP_DOCK_H */
