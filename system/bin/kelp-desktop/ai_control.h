/*
 * kelp-desktop :: ai_control.h
 * AI desktop actions: move_cursor, click, type, open_panel, screenshot.
 *
 * These actions are exposed as JSON-RPC methods via the gateway,
 * allowing the AI to autonomously control the desktop.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KELP_DESKTOP_AI_CONTROL_H
#define KELP_DESKTOP_AI_CONTROL_H

#include <stdbool.h>

struct kd_desktop;

/* ---- AI action types ---------------------------------------------------- */

typedef enum {
    KD_AI_MOVE_CURSOR,
    KD_AI_CLICK,
    KD_AI_TYPE,
    KD_AI_OPEN_PANEL,
    KD_AI_CLOSE_PANEL,
    KD_AI_SCREENSHOT,
    KD_AI_GET_STATE,
} kd_ai_action_type_t;

typedef struct {
    kd_ai_action_type_t action;
    double  x, y;               /* for move_cursor, click */
    char    text[4096];         /* for type */
    char    panel_name[64];     /* for open_panel, close_panel */
} kd_ai_action_t;

/* ---- API ---------------------------------------------------------------- */

void kd_ai_control_init(struct kd_desktop *d);
void kd_ai_control_shutdown(struct kd_desktop *d);

/** Execute an AI action on the desktop. Returns JSON result. */
char *kd_ai_control_execute(struct kd_desktop *d, const kd_ai_action_t *action);

/**
 * Process desktop.* JSON-RPC method.
 * Called by the gateway when it receives a desktop.* method on the
 * desktop control socket.
 * Returns a JSON result string (caller frees).
 */
char *kd_ai_control_dispatch(struct kd_desktop *d,
                               const char *method, const char *params_json);

#endif /* KELP_DESKTOP_AI_CONTROL_H */
