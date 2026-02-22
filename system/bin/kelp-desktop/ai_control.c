/*
 * kelp-desktop :: ai_control.c
 * AI desktop actions: move_cursor, click, type, open_panel, screenshot.
 *
 * The AI can call these methods via the gateway to visually control
 * the desktop — moving the cursor, opening panels, typing commands.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ai_control.h"
#include "desktop.h"
#include "cursor.h"
#include "chat.h"
#include "terminal.h"
#include "files.h"
#include "theme.h"

#include <cjson/cJSON.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <cairo/cairo-png.h>

/* ---- State -------------------------------------------------------------- */

static struct {
    kd_desktop_t *desktop;
    pthread_mutex_t lock;
} g_ai;

/* ---- Init/Shutdown ------------------------------------------------------ */

void kd_ai_control_init(kd_desktop_t *d)
{
    g_ai.desktop = d;
    pthread_mutex_init(&g_ai.lock, NULL);
}

void kd_ai_control_shutdown(kd_desktop_t *d)
{
    (void)d;
    pthread_mutex_destroy(&g_ai.lock);
}

/* ---- Panel name resolution ---------------------------------------------- */

static int resolve_panel(const char *name)
{
    if (!name) return -1;
    if (strcmp(name, "chat") == 0)     return KD_PANEL_CHAT;
    if (strcmp(name, "terminal") == 0) return KD_PANEL_TERMINAL;
    if (strcmp(name, "monitor") == 0)  return KD_PANEL_MONITOR;
    if (strcmp(name, "files") == 0)    return KD_PANEL_FILES;
    return -1;
}

/* ---- Desktop state as JSON ---------------------------------------------- */

static char *get_desktop_state(kd_desktop_t *d)
{
    cJSON *obj = cJSON_CreateObject();

    /* Screen. */
    cJSON_AddNumberToObject(obj, "screen_width", d->screen_w);
    cJSON_AddNumberToObject(obj, "screen_height", d->screen_h);

    /* Cursor. */
    cJSON *cursor = cJSON_AddObjectToObject(obj, "cursor");
    cJSON_AddNumberToObject(cursor, "x", d->mouse_x);
    cJSON_AddNumberToObject(cursor, "y", d->mouse_y);

    kd_ai_cursor_t *ai = kd_cursor_get_ai();
    cJSON *ai_cursor = cJSON_AddObjectToObject(obj, "ai_cursor");
    cJSON_AddBoolToObject(ai_cursor, "active", ai->active);
    cJSON_AddNumberToObject(ai_cursor, "x", ai->current_x);
    cJSON_AddNumberToObject(ai_cursor, "y", ai->current_y);

    /* Panels. */
    cJSON *panels = cJSON_AddArrayToObject(obj, "panels");
    const char *names[] = {"chat", "terminal", "monitor", "files"};
    for (int i = 0; i < KD_PANEL_COUNT; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", names[i]);
        cJSON_AddBoolToObject(p, "visible", d->panels[i].visible);
        cJSON_AddBoolToObject(p, "focused", d->panels[i].focused);
        cJSON_AddNumberToObject(p, "x", d->panels[i].x);
        cJSON_AddNumberToObject(p, "y", d->panels[i].y);
        cJSON_AddNumberToObject(p, "width", d->panels[i].w);
        cJSON_AddNumberToObject(p, "height", d->panels[i].h);
        cJSON_AddItemToArray(panels, p);
    }

    cJSON_AddBoolToObject(obj, "gateway_connected", d->gateway_connected);

    char *result = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return result;
}

/* ---- Screenshot --------------------------------------------------------- */

static char *take_screenshot(kd_desktop_t *d)
{
    /* Create an off-screen surface, render to it, export as PNG. */
    /* For now, return a placeholder — full screenshot requires
       access to the frame buffer which is complex. */
    (void)d;

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "format", "png");
    cJSON_AddNumberToObject(obj, "width", d->screen_w);
    cJSON_AddNumberToObject(obj, "height", d->screen_h);
    cJSON_AddStringToObject(obj, "note",
        "Screenshot capture will render the current frame to PNG. "
        "Use desktop.get_state for structural information instead.");

    char *result = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return result;
}

/* ---- Execute action ----------------------------------------------------- */

char *kd_ai_control_execute(kd_desktop_t *d, const kd_ai_action_t *action)
{
    cJSON *result = cJSON_CreateObject();

    switch (action->action) {
    case KD_AI_MOVE_CURSOR:
        kd_cursor_move_to(action->x, action->y);
        cJSON_AddBoolToObject(result, "ok", 1);
        cJSON_AddNumberToObject(result, "target_x", action->x);
        cJSON_AddNumberToObject(result, "target_y", action->y);
        d->needs_redraw = true;
        break;

    case KD_AI_CLICK:
        kd_cursor_move_to(action->x, action->y);
        kd_cursor_click();
        /* Simulate a click event after cursor arrives. */
        {
            SDL_Event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = SDL_MOUSEBUTTONDOWN;
            ev.button.x = (int)action->x;
            ev.button.y = (int)action->y;
            ev.button.button = SDL_BUTTON_LEFT;
            kd_desktop_handle_event(d, &ev);
        }
        cJSON_AddBoolToObject(result, "ok", 1);
        d->needs_redraw = true;
        break;

    case KD_AI_TYPE:
        /* Type text into focused panel. */
        if (d->focus_panel == KD_PANEL_CHAT) {
            kd_chat_handle_text(d, action->text);
        } else if (d->focus_panel == KD_PANEL_TERMINAL) {
            kd_terminal_inject_text(action->text);
        }
        cJSON_AddBoolToObject(result, "ok", 1);
        cJSON_AddStringToObject(result, "typed", action->text);
        d->needs_redraw = true;
        break;

    case KD_AI_OPEN_PANEL:
        {
            int panel = resolve_panel(action->panel_name);
            if (panel >= 0) {
                kd_desktop_open_panel(d, (kd_panel_type_t)panel);
                cJSON_AddBoolToObject(result, "ok", 1);
                cJSON_AddStringToObject(result, "panel", action->panel_name);
            } else {
                cJSON_AddBoolToObject(result, "ok", 0);
                cJSON_AddStringToObject(result, "error", "unknown panel");
            }
        }
        break;

    case KD_AI_CLOSE_PANEL:
        {
            int panel = resolve_panel(action->panel_name);
            if (panel >= 0) {
                kd_desktop_close_panel(d, (kd_panel_type_t)panel);
                cJSON_AddBoolToObject(result, "ok", 1);
            } else {
                cJSON_AddBoolToObject(result, "ok", 0);
                cJSON_AddStringToObject(result, "error", "unknown panel");
            }
        }
        break;

    case KD_AI_SCREENSHOT:
        {
            char *ss = take_screenshot(d);
            cJSON_Delete(result);
            return ss;
        }

    case KD_AI_GET_STATE:
        {
            char *state = get_desktop_state(d);
            cJSON_Delete(result);
            return state;
        }
    }

    char *json = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    return json;
}

/* ---- JSON-RPC dispatch -------------------------------------------------- */

char *kd_ai_control_dispatch(kd_desktop_t *d,
                               const char *method, const char *params_json)
{
    cJSON *params = params_json ? cJSON_Parse(params_json) : NULL;

    kd_ai_action_t action;
    memset(&action, 0, sizeof(action));

    if (strcmp(method, "desktop.move_cursor") == 0) {
        action.action = KD_AI_MOVE_CURSOR;
        if (params) {
            action.x = cJSON_GetObjectItem(params, "x")
                ? cJSON_GetObjectItem(params, "x")->valuedouble : 0;
            action.y = cJSON_GetObjectItem(params, "y")
                ? cJSON_GetObjectItem(params, "y")->valuedouble : 0;
        }
    } else if (strcmp(method, "desktop.click") == 0) {
        action.action = KD_AI_CLICK;
        if (params) {
            action.x = cJSON_GetObjectItem(params, "x")
                ? cJSON_GetObjectItem(params, "x")->valuedouble : 0;
            action.y = cJSON_GetObjectItem(params, "y")
                ? cJSON_GetObjectItem(params, "y")->valuedouble : 0;
        }
    } else if (strcmp(method, "desktop.type") == 0) {
        action.action = KD_AI_TYPE;
        if (params) {
            cJSON *text = cJSON_GetObjectItem(params, "text");
            if (text && text->valuestring)
                snprintf(action.text, sizeof(action.text), "%s", text->valuestring);
        }
    } else if (strcmp(method, "desktop.open_panel") == 0) {
        action.action = KD_AI_OPEN_PANEL;
        if (params) {
            cJSON *name = cJSON_GetObjectItem(params, "name");
            if (name && name->valuestring)
                snprintf(action.panel_name, sizeof(action.panel_name),
                         "%s", name->valuestring);
        }
    } else if (strcmp(method, "desktop.close_panel") == 0) {
        action.action = KD_AI_CLOSE_PANEL;
        if (params) {
            cJSON *name = cJSON_GetObjectItem(params, "name");
            if (name && name->valuestring)
                snprintf(action.panel_name, sizeof(action.panel_name),
                         "%s", name->valuestring);
        }
    } else if (strcmp(method, "desktop.screenshot") == 0) {
        action.action = KD_AI_SCREENSHOT;
    } else if (strcmp(method, "desktop.get_state") == 0) {
        action.action = KD_AI_GET_STATE;
    } else {
        cJSON_Delete(params);
        return strdup("{\"error\":\"unknown desktop method\"}");
    }

    cJSON_Delete(params);

    pthread_mutex_lock(&g_ai.lock);
    char *result = kd_ai_control_execute(d, &action);
    pthread_mutex_unlock(&g_ai.lock);

    return result;
}
