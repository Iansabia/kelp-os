/*
 * kelp-desktop :: monitor.h
 * System monitor: /proc/kelp/* metrics, animated bar/line charts.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_MONITOR_H
#define KELP_DESKTOP_MONITOR_H

#include <cairo/cairo.h>
#include <stdbool.h>
#include <stdint.h>

struct kd_desktop;

/* ---- API ---------------------------------------------------------------- */

void kd_monitor_init(struct kd_desktop *d);
void kd_monitor_shutdown(struct kd_desktop *d);
void kd_monitor_update(struct kd_desktop *d, uint32_t now_ms);
void kd_monitor_draw(struct kd_desktop *d, cairo_t *cr,
                      double x, double y, double w, double h);

#endif /* KELP_DESKTOP_MONITOR_H */
