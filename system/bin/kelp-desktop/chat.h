/*
 * kelp-desktop :: chat.h
 * AI chat panel: message history, input box, streaming text, code blocks.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_CHAT_H
#define KELP_DESKTOP_CHAT_H

#include <kelp/config.h>
#include <cairo/cairo.h>
#include <SDL2/SDL.h>
#include <stdbool.h>

struct kd_desktop;

/* ---- Chat message ------------------------------------------------------- */

typedef enum {
    KD_MSG_USER,
    KD_MSG_ASSISTANT,
    KD_MSG_SYSTEM,
    KD_MSG_ERROR
} kd_msg_type_t;

typedef struct {
    kd_msg_type_t type;
    char         *text;
    time_t        timestamp;
} kd_chat_msg_t;

/* ---- API ---------------------------------------------------------------- */

void kd_chat_init(struct kd_desktop *d, kelp_config_t *cfg);
void kd_chat_shutdown(struct kd_desktop *d);
void kd_chat_connect_gateway(struct kd_desktop *d, kelp_config_t *cfg);
void kd_chat_update(struct kd_desktop *d, uint32_t now_ms);
void kd_chat_draw(struct kd_desktop *d, cairo_t *cr,
                   double x, double y, double w, double h);

void kd_chat_handle_key(struct kd_desktop *d, SDL_KeyboardEvent *key);
void kd_chat_handle_text(struct kd_desktop *d, const char *text);
void kd_chat_handle_click(struct kd_desktop *d, double px, double py);

/** Add a message programmatically (e.g. from AI control). */
void kd_chat_add_message(kd_msg_type_t type, const char *text);

/** Send a message as if the user typed it. */
void kd_chat_send_text(const char *text);

#endif /* KELP_DESKTOP_CHAT_H */
