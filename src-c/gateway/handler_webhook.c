#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gateway.h"
#include "openclaw.h"
#include "config.h"
#include "auth.h"
#include "http.h"
#include "stream.h"
#include "json.h"
#include "cJSON.h"

/* Collect streamed text into a buffer */
typedef struct {
    char   *buf;
    size_t  len;
    size_t  cap;
} collect_ctx_t;

static void collect_text(const char *text, size_t len, void *userdata) {
    collect_ctx_t *ctx = (collect_ctx_t *)userdata;
    if (ctx->len + len + 1 > ctx->cap) {
        size_t new_cap = ctx->cap == 0 ? 4096 : ctx->cap * 2;
        while (new_cap < ctx->len + len + 1) new_cap *= 2;
        char *new_buf = realloc(ctx->buf, new_cap);
        if (!new_buf) return;
        ctx->buf = new_buf;
        ctx->cap = new_cap;
    }
    memcpy(ctx->buf + ctx->len, text, len);
    ctx->len += len;
    ctx->buf[ctx->len] = '\0';
}

int handler_webhook(connection_t *conn, http_request_t *req,
                    http_response_build_t *resp, void *ctx) {
    (void)conn;
    gateway_ctx_t *gw = (gateway_ctx_t *)ctx;

    if (!req->body || req->body_len == 0) {
        http_response_set_status(resp, 400, "Bad Request");
        http_response_set_json(resp, "{\"error\":\"Empty body\"}");
        return OC_OK;
    }

    /* Parse incoming message */
    cJSON *json = cJSON_Parse(req->body);
    if (!json) {
        http_response_set_status(resp, 400, "Bad Request");
        http_response_set_json(resp, "{\"error\":\"Invalid JSON\"}");
        return OC_OK;
    }

    const char *message = json_get_str(json, "message");
    if (!message || !*message) {
        cJSON_Delete(json);
        http_response_set_status(resp, 400, "Bad Request");
        http_response_set_json(resp, "{\"error\":\"Missing 'message' field\"}");
        return OC_OK;
    }

    const char *session_id = json_get_str(json, "session_id");
    /* session_id is optional — we'll implement session continuity later */

    /* Determine provider and resolve key */
    provider_t provider = auth_parse_provider(gw->cfg->default_provider);
    const char *api_key = auth_resolve(gw->cfg, provider);
    if (!api_key) {
        cJSON_Delete(json);
        http_response_set_status(resp, 500, "Internal Server Error");
        http_response_set_json(resp, "{\"error\":\"No API key configured\"}");
        return OC_OK;
    }

    /* Build AI request */
    const char *model = (provider == PROVIDER_ANTHROPIC) ?
        gw->cfg->anthropic_model : gw->cfg->openai_model;

    cJSON *ai_body;
    const char *url;
    if (provider == PROVIDER_ANTHROPIC) {
        url = "https://api.anthropic.com/v1/messages";
        ai_body = json_build_anthropic_request(model, gw->cfg->system_prompt,
                                                message, gw->cfg->max_tokens,
                                                gw->cfg->temperature);
    } else {
        url = "https://api.openai.com/v1/chat/completions";
        ai_body = json_build_openai_request(model, gw->cfg->system_prompt,
                                             message, gw->cfg->max_tokens,
                                             gw->cfg->temperature);
    }

    /* Stream the AI response, collecting text */
    collect_ctx_t collect = {0};
    stream_ctx_t sctx = {
        .provider = provider,
        .on_text = collect_text,
        .on_done = NULL,
        .on_error = NULL,
        .userdata = &collect,
    };
    stream_ctx_init(&sctx);

    int rc = http_stream_post(url, api_key, ai_body, &sctx, provider);

    cJSON_Delete(ai_body);
    stream_ctx_cleanup(&sctx);

    /* Build response */
    cJSON *resp_json = cJSON_CreateObject();
    if (rc == OC_OK && collect.buf) {
        cJSON_AddStringToObject(resp_json, "response", collect.buf);
        if (session_id) cJSON_AddStringToObject(resp_json, "session_id", session_id);
        cJSON_AddStringToObject(resp_json, "model", model);
    } else {
        http_response_set_status(resp, 502, "Bad Gateway");
        cJSON_AddStringToObject(resp_json, "error", "AI API request failed");
    }

    char *resp_body = cJSON_PrintUnformatted(resp_json);
    http_response_set_json(resp, resp_body);
    free(resp_body);
    cJSON_Delete(resp_json);
    cJSON_Delete(json);
    free(collect.buf);

    return OC_OK;
}
