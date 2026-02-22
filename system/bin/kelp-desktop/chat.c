/*
 * kelp-desktop :: chat.c
 * AI chat panel: message history, input box, streaming text, code blocks.
 *
 * Ported from kelp-tui chat logic, rendered with Cairo + Pango.
 *
 * SPDX-License-Identifier: MIT
 */

#include "chat.h"
#include "desktop.h"
#include "render.h"
#include "theme.h"
#include "animation.h"

#include <kelp/kelp.h>
#include <kelp/config.h>
#include <kelp/paths.h>
#include <kelp/log.h>

#include <cjson/cJSON.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* ---- Constants ---------------------------------------------------------- */

#define MAX_CHAT_MESSAGES  2048
#define MAX_INPUT_LEN      4096
#define INPUT_BOX_HEIGHT   48

/* ---- State -------------------------------------------------------------- */

static struct {
    kelp_config_t *cfg;

    /* Messages. */
    kd_chat_msg_t messages[MAX_CHAT_MESSAGES];
    int           msg_count;
    int           scroll_offset;

    /* Input. */
    char   input_buf[MAX_INPUT_LEN];
    int    input_len;
    int    input_pos;

    /* Gateway. */
    bool   connected;

    /* Async response. */
    bool   waiting;
    int    think_frame;
    char  *pending_response;
    bool   response_ready;
    bool   response_error;
    pthread_mutex_t async_lock;

    /* Streaming text reveal. */
    char  *stream_text;
    int    stream_pos;
    int    stream_msg_idx;

    /* Cursor blink. */
    uint32_t cursor_blink_ms;
    bool     cursor_visible;
} g_chat;

/* ---- Forward declarations ----------------------------------------------- */

static char *rpc_call(const char *method, cJSON *params);

/* ---- Message management ------------------------------------------------- */

void kd_chat_add_message(kd_msg_type_t type, const char *text)
{
    if (g_chat.msg_count >= MAX_CHAT_MESSAGES) {
        free(g_chat.messages[0].text);
        memmove(&g_chat.messages[0], &g_chat.messages[1],
                sizeof(kd_chat_msg_t) * (MAX_CHAT_MESSAGES - 1));
        g_chat.msg_count = MAX_CHAT_MESSAGES - 1;
    }
    kd_chat_msg_t *msg = &g_chat.messages[g_chat.msg_count];
    msg->type = type;
    msg->text = strdup(text);
    msg->timestamp = time(NULL);
    g_chat.msg_count++;
    g_chat.scroll_offset = 0;
}

/* ---- Gateway RPC -------------------------------------------------------- */

static char *rpc_call(const char *method, cJSON *params)
{
    const char *sock_path = g_chat.cfg ? g_chat.cfg->gateway.socket_path : NULL;
    char *default_sock = NULL;
    if (!sock_path) {
        default_sock = kelp_paths_socket();
        sock_path = default_sock;
    }
    if (!sock_path) {
        free(default_sock);
        g_chat.connected = false;
        return NULL;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { free(default_sock); g_chat.connected = false; return NULL; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);
    free(default_sock);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        g_chat.connected = false;
        return NULL;
    }
    g_chat.connected = true;

    static int rpc_id = 1;
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", rpc_id++);
    cJSON_AddStringToObject(req, "method", method);
    if (params)
        cJSON_AddItemToObject(req, "params", cJSON_Duplicate(params, 1));

    char *payload = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!payload) { close(fd); return NULL; }

    size_t plen = strlen(payload);
    char *sendbuf = malloc(plen + 2);
    if (!sendbuf) { free(payload); close(fd); return NULL; }
    memcpy(sendbuf, payload, plen);
    sendbuf[plen] = '\n';
    sendbuf[plen + 1] = '\0';
    free(payload);

    ssize_t sent = 0, total = (ssize_t)(plen + 1);
    while (sent < total) {
        ssize_t n = write(fd, sendbuf + sent, (size_t)(total - sent));
        if (n <= 0) { free(sendbuf); close(fd); return NULL; }
        sent += n;
    }
    free(sendbuf);

    /* Read response. */
    kelp_str_t resp = kelp_str_new();
    char buf[4096];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        kelp_str_append(&resp, buf, (size_t)n);
        if (resp.len > 0 && resp.data[resp.len - 1] == '\n') break;
    }
    close(fd);

    if (resp.len == 0) { kelp_str_free(&resp); return NULL; }
    kelp_str_trim(&resp);
    char *result = resp.data;
    resp.data = NULL;
    return result;
}

/* ---- Async response thread ---------------------------------------------- */

static void *response_thread_fn(void *arg)
{
    char *msg_text = (char *)arg;
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "message", msg_text);
    cJSON_AddStringToObject(params, "channel_id", "desktop");
    cJSON_AddStringToObject(params, "user_id", "local");

    char *resp_str = rpc_call("chat.send", params);
    cJSON_Delete(params);
    free(msg_text);

    pthread_mutex_lock(&g_chat.async_lock);
    if (!resp_str) {
        g_chat.pending_response = strdup("No response from gateway.");
        g_chat.response_error = true;
    } else {
        cJSON *resp = kelp_json_parse(resp_str);
        free(resp_str);
        if (!resp) {
            g_chat.pending_response = strdup("Failed to parse response.");
            g_chat.response_error = true;
        } else {
            cJSON *error = cJSON_GetObjectItem(resp, "error");
            if (error) {
                const char *errmsg = kelp_json_get_string(error, "message");
                g_chat.pending_response = strdup(errmsg ? errmsg : "unknown error");
                g_chat.response_error = true;
            } else {
                cJSON *result_obj = cJSON_GetObjectItem(resp, "result");
                const char *content = result_obj
                    ? kelp_json_get_string(result_obj, "content") : NULL;
                g_chat.pending_response = strdup(content ? content : "(empty)");
                g_chat.response_error = false;
            }
            cJSON_Delete(resp);
        }
    }
    g_chat.response_ready = true;
    pthread_mutex_unlock(&g_chat.async_lock);
    return NULL;
}

static void send_message(void)
{
    if (g_chat.input_len == 0 || g_chat.waiting) return;

    g_chat.input_buf[g_chat.input_len] = '\0';
    char *msg_text = strdup(g_chat.input_buf);
    if (!msg_text) return;

    g_chat.input_len = 0;
    g_chat.input_pos = 0;
    g_chat.input_buf[0] = '\0';

    /* Commands. */
    if (strcmp(msg_text, "/quit") == 0 || strcmp(msg_text, "/exit") == 0) {
        free(msg_text);
        return;
    }
    if (strcmp(msg_text, "/clear") == 0) {
        for (int i = 0; i < g_chat.msg_count; i++)
            free(g_chat.messages[i].text);
        g_chat.msg_count = 0;
        g_chat.scroll_offset = 0;
        free(msg_text);
        return;
    }

    kd_chat_add_message(KD_MSG_USER, msg_text);

    g_chat.waiting = true;
    g_chat.think_frame = 0;
    g_chat.response_ready = false;
    g_chat.response_error = false;

    pthread_t tid;
    if (pthread_create(&tid, NULL, response_thread_fn, msg_text) == 0)
        pthread_detach(tid);
    else {
        kd_chat_add_message(KD_MSG_ERROR, "Failed to start response thread.");
        g_chat.waiting = false;
        free(msg_text);
    }
}

void kd_chat_send_text(const char *text)
{
    kd_chat_add_message(KD_MSG_USER, text);

    char *msg_copy = strdup(text);
    if (!msg_copy) return;

    g_chat.waiting = true;
    g_chat.think_frame = 0;
    g_chat.response_ready = false;
    g_chat.response_error = false;

    pthread_t tid;
    if (pthread_create(&tid, NULL, response_thread_fn, msg_copy) == 0)
        pthread_detach(tid);
    else {
        kd_chat_add_message(KD_MSG_ERROR, "Failed to start response thread.");
        g_chat.waiting = false;
        free(msg_copy);
    }
}

/* ---- Init/Shutdown ------------------------------------------------------ */

void kd_chat_init(kd_desktop_t *d, kelp_config_t *cfg)
{
    (void)d;
    memset(&g_chat, 0, sizeof(g_chat));
    g_chat.cfg = cfg;
    pthread_mutex_init(&g_chat.async_lock, NULL);
    g_chat.cursor_blink_ms = 0;
    g_chat.cursor_visible = true;

    kd_chat_add_message(KD_MSG_SYSTEM,
        "Welcome to Kelp OS. I'm your AI assistant. "
        "I can control this computer \xe2\x80\x94 open apps, run commands, "
        "browse files. Just ask.");
}

void kd_chat_shutdown(kd_desktop_t *d)
{
    (void)d;
    for (int i = 0; i < g_chat.msg_count; i++)
        free(g_chat.messages[i].text);
    free(g_chat.pending_response);
    free(g_chat.stream_text);
    pthread_mutex_destroy(&g_chat.async_lock);
}

void kd_chat_connect_gateway(kd_desktop_t *d, kelp_config_t *cfg)
{
    /* Try a health check RPC. */
    char *resp = rpc_call("health", NULL);
    if (resp) {
        g_chat.connected = true;
        d->gateway_connected = true;
        kd_chat_add_message(KD_MSG_SYSTEM, "Gateway connected.");
        free(resp);
    } else {
        g_chat.connected = false;
        d->gateway_connected = false;
    }
}

/* ---- Update ------------------------------------------------------------- */

void kd_chat_update(kd_desktop_t *d, uint32_t now_ms)
{
    /* Async response handling. */
    if (g_chat.waiting) {
        pthread_mutex_lock(&g_chat.async_lock);
        if (g_chat.response_ready) {
            g_chat.waiting = false;
            if (g_chat.pending_response) {
                if (g_chat.response_error) {
                    kd_chat_add_message(KD_MSG_ERROR, g_chat.pending_response);
                    free(g_chat.pending_response);
                } else {
                    g_chat.stream_text = g_chat.pending_response;
                    g_chat.stream_pos = 0;
                    kd_chat_add_message(KD_MSG_ASSISTANT, "");
                    g_chat.stream_msg_idx = g_chat.msg_count - 1;
                }
                g_chat.pending_response = NULL;
            }
            d->needs_redraw = true;
        } else {
            g_chat.think_frame++;
            d->needs_redraw = true;
        }
        pthread_mutex_unlock(&g_chat.async_lock);
    }

    /* Streaming text reveal. */
    if (g_chat.stream_text) {
        int total_len = (int)strlen(g_chat.stream_text);
        int remaining = total_len - g_chat.stream_pos;
        if (remaining > 0) {
            int advance = 6;
            if (total_len > 2000) advance = 20;
            else if (total_len > 500) advance = 12;
            if (advance > remaining) advance = remaining;
            g_chat.stream_pos += advance;

            kd_chat_msg_t *msg = &g_chat.messages[g_chat.stream_msg_idx];
            free(msg->text);
            msg->text = malloc((size_t)g_chat.stream_pos + 1);
            if (msg->text) {
                memcpy(msg->text, g_chat.stream_text, (size_t)g_chat.stream_pos);
                msg->text[g_chat.stream_pos] = '\0';
            }
            d->needs_redraw = true;
        } else {
            free(g_chat.stream_text);
            g_chat.stream_text = NULL;
            d->needs_redraw = true;
        }
    }

    /* Cursor blink. */
    if (now_ms - g_chat.cursor_blink_ms > 500) {
        g_chat.cursor_visible = !g_chat.cursor_visible;
        g_chat.cursor_blink_ms = now_ms;
        d->needs_redraw = true;
    }

    /* Retry gateway connection. */
    if (!g_chat.connected && !g_chat.waiting) {
        static uint32_t last_retry = 0;
        if (now_ms - last_retry > 5000) {
            last_retry = now_ms;
            char *resp = rpc_call("health", NULL);
            if (resp) {
                g_chat.connected = true;
                d->gateway_connected = true;
                free(resp);
            }
        }
    }
}

/* ---- Drawing ------------------------------------------------------------ */

void kd_chat_draw(kd_desktop_t *d, cairo_t *cr,
                   double x, double y, double w, double h)
{
    (void)d;

    double pad = KD_PANEL_PADDING;
    double input_y = y + h - INPUT_BOX_HEIGHT - pad;
    double chat_y = y + pad;
    double chat_h = input_y - chat_y - pad;
    double chat_w = w - pad * 2;

    /* Draw messages. */
    double my = chat_y;
    int start = 0;
    if (g_chat.msg_count > 0) {
        /* Simple scroll: draw from end and measure upward. */
        /* For now, draw from start — improve with scrollback later. */
        start = g_chat.msg_count > 50 ? g_chat.msg_count - 50 : 0;
    }

    cairo_save(cr);
    cairo_rectangle(cr, x + pad, chat_y, chat_w, chat_h);
    cairo_clip(cr);

    for (int i = start; i < g_chat.msg_count && my < chat_y + chat_h; i++) {
        kd_chat_msg_t *msg = &g_chat.messages[i];

        /* Label. */
        const char *label = NULL;
        kd_color_t label_color = KD_TEXT_DIM;
        kd_color_t text_color = KD_TEXT_PRIMARY;

        switch (msg->type) {
        case KD_MSG_USER:
            label = "you";
            label_color = KD_TEXT_DIM;
            text_color = KD_TEXT_PRIMARY;
            break;
        case KD_MSG_ASSISTANT:
            label = "kelp";
            label_color = KD_ACCENT_GREEN;
            text_color = KD_TEXT_PRIMARY;
            break;
        case KD_MSG_SYSTEM:
            label_color = KD_TEXT_DIM;
            text_color = KD_TEXT_SECONDARY;
            break;
        case KD_MSG_ERROR:
            label = "error";
            label_color = KD_STATUS_ERROR;
            text_color = KD_STATUS_ERROR;
            break;
        }

        if (label) {
            int lh = kd_draw_text_bold(cr, label, x + pad, my,
                                        KD_FONT_FAMILY, KD_FONT_SIZE_SMALL,
                                        label_color, 0);
            my += lh + 2;
        }

        /* Message text. */
        if (msg->text && msg->text[0]) {
            /* Check if it's a code block. */
            bool is_code = (strstr(msg->text, "```") != NULL);
            if (is_code) {
                int th = kd_draw_mono_text(cr, msg->text, x + pad + 8, my,
                                            KD_FONT_SIZE_MONO, KD_TEXT_CODE,
                                            chat_w - 16);
                my += th;
            } else {
                int th = kd_draw_text(cr, msg->text, x + pad, my,
                                       KD_FONT_FAMILY, KD_FONT_SIZE_NORMAL,
                                       text_color, chat_w);
                my += th;
            }
        }

        my += 12; /* spacing between messages */
    }

    /* Thinking indicator. */
    if (g_chat.waiting && !g_chat.stream_text) {
        const char *dots[] = {".", "..", "..."};
        int di = (g_chat.think_frame / 10) % 3;
        char think[32];
        snprintf(think, sizeof(think), "thinking%s", dots[di]);
        kd_draw_text(cr, think, x + pad, my,
                      KD_FONT_FAMILY, KD_FONT_SIZE_NORMAL,
                      KD_TEXT_DIM, 0);
    }

    cairo_restore(cr);

    /* Input box. */
    kd_fill_rounded_rect(cr, x + pad, input_y, chat_w, INPUT_BOX_HEIGHT,
                          6.0, KD_BG_SURFACE);
    kd_draw_border(cr, x + pad, input_y, chat_w, INPUT_BOX_HEIGHT,
                    6.0, KD_BORDER);

    /* Prompt. */
    kd_draw_text_bold(cr, ">", x + pad + 12, input_y + 14,
                       KD_FONT_MONO, KD_FONT_SIZE_NORMAL,
                       KD_ACCENT_GREEN, 0);

    /* Input text. */
    if (g_chat.input_len > 0) {
        g_chat.input_buf[g_chat.input_len] = '\0';
        kd_draw_text(cr, g_chat.input_buf, x + pad + 28, input_y + 14,
                      KD_FONT_FAMILY, KD_FONT_SIZE_NORMAL,
                      KD_TEXT_PRIMARY, chat_w - 44);
    } else {
        kd_draw_text(cr, "Type a message...", x + pad + 28, input_y + 14,
                      KD_FONT_FAMILY, KD_FONT_SIZE_NORMAL,
                      KD_TEXT_DIM, 0);
    }

    /* Blinking cursor. */
    if (g_chat.cursor_visible && d->focus_panel == KD_PANEL_CHAT) {
        int cw_px = 0, ch_px = 0;
        if (g_chat.input_len > 0) {
            char tmp[MAX_INPUT_LEN];
            memcpy(tmp, g_chat.input_buf, (size_t)g_chat.input_pos);
            tmp[g_chat.input_pos] = '\0';
            kd_measure_text(cr, tmp, KD_FONT_FAMILY,
                             KD_FONT_SIZE_NORMAL, &cw_px, &ch_px);
        }
        double cx = x + pad + 28 + cw_px;
        double cy = input_y + 14;
        kd_fill_rect(cr, cx, cy, 2, 16, KD_TEXT_PRIMARY);
    }
}

/* ---- Input handling ----------------------------------------------------- */

void kd_chat_handle_key(kd_desktop_t *d, SDL_KeyboardEvent *key)
{
    if (key->type != SDL_KEYDOWN) return;

    SDL_Keycode sym = key->keysym.sym;
    Uint16 mod = key->keysym.mod;

    switch (sym) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        send_message();
        d->needs_redraw = true;
        break;

    case SDLK_BACKSPACE:
        if (g_chat.input_pos > 0) {
            memmove(&g_chat.input_buf[g_chat.input_pos - 1],
                    &g_chat.input_buf[g_chat.input_pos],
                    (size_t)(g_chat.input_len - g_chat.input_pos));
            g_chat.input_pos--;
            g_chat.input_len--;
            g_chat.input_buf[g_chat.input_len] = '\0';
            d->needs_redraw = true;
        }
        break;

    case SDLK_DELETE:
        if (g_chat.input_pos < g_chat.input_len) {
            memmove(&g_chat.input_buf[g_chat.input_pos],
                    &g_chat.input_buf[g_chat.input_pos + 1],
                    (size_t)(g_chat.input_len - g_chat.input_pos - 1));
            g_chat.input_len--;
            g_chat.input_buf[g_chat.input_len] = '\0';
            d->needs_redraw = true;
        }
        break;

    case SDLK_LEFT:
        if (g_chat.input_pos > 0) { g_chat.input_pos--; d->needs_redraw = true; }
        break;

    case SDLK_RIGHT:
        if (g_chat.input_pos < g_chat.input_len) { g_chat.input_pos++; d->needs_redraw = true; }
        break;

    case SDLK_HOME:
    case SDLK_a:
        if (sym == SDLK_a && !(mod & KMOD_CTRL)) break;
        g_chat.input_pos = 0;
        d->needs_redraw = true;
        break;

    case SDLK_END:
    case SDLK_e:
        if (sym == SDLK_e && !(mod & KMOD_CTRL)) break;
        g_chat.input_pos = g_chat.input_len;
        d->needs_redraw = true;
        break;

    case SDLK_u:
        if (mod & KMOD_CTRL) {
            g_chat.input_len = 0;
            g_chat.input_pos = 0;
            g_chat.input_buf[0] = '\0';
            d->needs_redraw = true;
        }
        break;

    case SDLK_PAGEUP:
        g_chat.scroll_offset += 5;
        d->needs_redraw = true;
        break;

    case SDLK_PAGEDOWN:
        g_chat.scroll_offset -= 5;
        if (g_chat.scroll_offset < 0) g_chat.scroll_offset = 0;
        d->needs_redraw = true;
        break;

    default:
        break;
    }

    /* Reset cursor blink on any key. */
    g_chat.cursor_visible = true;
    g_chat.cursor_blink_ms = kd_time_ms();
}

void kd_chat_handle_text(kd_desktop_t *d, const char *text)
{
    int len = (int)strlen(text);
    if (g_chat.input_len + len >= MAX_INPUT_LEN - 1) return;

    memmove(&g_chat.input_buf[g_chat.input_pos + len],
            &g_chat.input_buf[g_chat.input_pos],
            (size_t)(g_chat.input_len - g_chat.input_pos));
    memcpy(&g_chat.input_buf[g_chat.input_pos], text, (size_t)len);
    g_chat.input_pos += len;
    g_chat.input_len += len;
    g_chat.input_buf[g_chat.input_len] = '\0';
    d->needs_redraw = true;

    g_chat.cursor_visible = true;
    g_chat.cursor_blink_ms = kd_time_ms();
}

void kd_chat_handle_click(kd_desktop_t *d, double px, double py)
{
    (void)d;
    (void)px;
    (void)py;
    /* TODO: click-to-position in input box */
}
