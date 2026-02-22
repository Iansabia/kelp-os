/*
 * kelp-desktop :: files.h
 * File browser: directory listing, breadcrumb nav, file preview.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_FILES_H
#define KELP_DESKTOP_FILES_H

#include <cairo/cairo.h>
#include <SDL2/SDL.h>
#include <stdbool.h>

struct kd_desktop;

/* ---- API ---------------------------------------------------------------- */

void kd_files_init(struct kd_desktop *d);
void kd_files_shutdown(struct kd_desktop *d);
void kd_files_draw(struct kd_desktop *d, cairo_t *cr,
                    double x, double y, double w, double h);

void kd_files_handle_key(struct kd_desktop *d, SDL_KeyboardEvent *key);
void kd_files_handle_click(struct kd_desktop *d, double px, double py);

/** Navigate to a directory (for AI control). */
void kd_files_navigate(const char *path);

#endif /* KELP_DESKTOP_FILES_H */
