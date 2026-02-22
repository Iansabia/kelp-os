/*
 * kelp-desktop :: terminal.h
 * Terminal emulator: basic VT100, PTY via libkelp-process.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_TERMINAL_H
#define KELP_DESKTOP_TERMINAL_H

#include <cairo/cairo.h>
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

struct kd_desktop;

/* ---- API ---------------------------------------------------------------- */

void kd_terminal_init(struct kd_desktop *d);
void kd_terminal_shutdown(struct kd_desktop *d);
void kd_terminal_update(struct kd_desktop *d, uint32_t now_ms);
void kd_terminal_draw(struct kd_desktop *d, cairo_t *cr,
                       double x, double y, double w, double h);

void kd_terminal_handle_key(struct kd_desktop *d, SDL_KeyboardEvent *key);
void kd_terminal_handle_text(struct kd_desktop *d, const char *text);
void kd_terminal_handle_click(struct kd_desktop *d, double px, double py);

/** Write text to the terminal as if typed (for AI control). */
void kd_terminal_inject_text(const char *text);

/** Read the current terminal output buffer. */
const char *kd_terminal_get_output(void);

#endif /* KELP_DESKTOP_TERMINAL_H */
