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

typedef struct {
    char   *buf;
    size_t  len;
    size_t  cap;
    int     input_tokens;
    int     output_tokens;
} chat_collect_t;

static void chat_collect_text(const char *text, size_t len, void *userdata) {
    chat_collect_t *ctx = (chat_collect_t *)userdata;
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

static void chat_collect_done(int input_tokens, int output_tokens, void *userdata) {
    chat_collect_t *ctx = (chat_collect_t *)userdata;
    ctx->input_tokens = input_tokens;
    ctx->output_tokens = output_tokens;
}

int handler_chat(connection_t *conn, http_request_t *req,
                 http_response_build_t *resp, void *ctx) {
    (void)conn;
    gateway_ctx_t *gw = (gateway_ctx_t *)ctx;

    if (!req->body || req->body_len == 0) {
        http_response_set_status(resp, 400, "Bad Request");
        http_response_set_json(resp, "{\"error\":{\"message\":\"Empty body\",\"type\":\"invalid_request_error\"}}");
        return OC_OK;
    }

    cJSON *json = cJSON_Parse(req->body);
    if (!json) {
        http_response_set_status(resp, 400, "Bad Request");
        http_response_set_json(resp, "{\"error\":{\"message\":\"Invalid JSON\",\"type\":\"invalid_request_error\"}}");
        return OC_OK;
    }

    /* Extract messages array */
    const cJSON *messages = cJSON_GetObjectItemCaseSensitive(json, "messages");
    if (!cJSON_IsArray(messages) || cJSON_GetArraySize(messages) == 0) {
        cJSON_Delete(json);
        http_response_set_status(resp, 400, "Bad Request");
        http_response_set_json(resp, "{\"error\":{\"message\":\"Missing messages array\",\"type\":\"invalid_request_error\"}}");
        return OC_OK;
    }

    /* Get the last user message */
    const char *user_msg = NULL;
    const char *system_msg = NULL;
    const cJSON *msg_item;
    cJSON_ArrayForEach(msg_item, messages) {
        const char *role = json_get_str(msg_item, "role");
        const char *content = json_get_str(msg_item, "content");
        if (role && content) {
            if (strcmp(role, "user") == 0) user_msg = content;
            else if (strcmp(role, "system") == 0) system_msg = content;
        }
    }

    if (!user_msg) {
        cJSON_Delete(json);
        http_response_set_status(resp, 400, "Bad Request");
        http_response_set_json(resp, "{\"error\":{\"message\":\"No user message found\",\"type\":\"invalid_request_error\"}}");
        return OC_OK;
    }

    /* Use the system message from the request, or fall back to config */
    if (!system_msg) system_msg = gw->cfg->system_prompt;

    /* Resolve provider and model */
    const char *req_model = json_get_str(json, "model");
    provider_t provider = auth_parse_provider(gw->cfg->default_provider);
    const char *model;

    if (req_model) {
        /* Determine provider from model name */
        if (strncmp(req_model, "claude", 6) == 0) {
            provider = PROVIDER_ANTHROPIC;
            model = req_model;
        } else if (strncmp(req_model, "gpt", 3) == 0) {
            provider = PROVIDER_OPENAI;
            model = req_model;
        } else {
            model = req_model;
        }
    } else {
        model = (provider == PROVIDER_ANTHROPIC) ?
            gw->cfg->anthropic_model : gw->cfg->openai_model;
    }

    const char *api_key = auth_resolve(gw->cfg, provider);
    if (!api_key) {
        cJSON_Delete(json);
        http_response_set_status(resp, 500, "Internal Server Error");
        http_response_set_json(resp, "{\"error\":{\"message\":\"No API key configured\",\"type\":\"server_error\"}}");
        return OC_OK;
    }

    int max_tokens = json_get_int(json, "max_tokens", gw->cfg->max_tokens);
    double temperature = json_get_double(json, "temperature", gw->cfg->temperature);

    /* Build upstream request */
    cJSON *ai_body;
    const char *url;
    if (provider == PROVIDER_ANTHROPIC) {
        url = "https://api.anthropic.com/v1/messages";
        ai_body = json_build_anthropic_request(model, system_msg,
                                                user_msg, max_tokens, temperature);
    } else {
        url = "https://api.openai.com/v1/chat/completions";
        ai_body = json_build_openai_request(model, system_msg,
                                             user_msg, max_tokens, temperature);
    }

    /* Stream and collect */
    chat_collect_t collect = {0};
    stream_ctx_t sctx = {
        .provider = provider,
        .on_text = chat_collect_text,
        .on_done = chat_collect_done,
        .on_error = NULL,
        .userdata = &collect,
    };
    stream_ctx_init(&sctx);

    int rc = http_stream_post(url, api_key, ai_body, &sctx, provider);

    cJSON_Delete(ai_body);
    stream_ctx_cleanup(&sctx);

    /* Build OpenAI-compatible response */
    cJSON *resp_json = cJSON_CreateObject();

    if (rc == OC_OK && collect.buf) {
        cJSON_AddStringToObject(resp_json, "id", "chatcmpl-openclaw");
        cJSON_AddStringToObject(resp_json, "object", "chat.completion");
        cJSON_AddStringToObject(resp_json, "model", model);

        cJSON *choices = cJSON_AddArrayToObject(resp_json, "choices");
        cJSON *choice = cJSON_CreateObject();
        cJSON_AddNumberToObject(choice, "index", 0);

        cJSON *msg_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_obj, "role", "assistant");
        cJSON_AddStringToObject(msg_obj, "content", collect.buf);
        cJSON_AddItemToObject(choice, "message", msg_obj);
        cJSON_AddStringToObject(choice, "finish_reason", "stop");
        cJSON_AddItemToArray(choices, choice);

        cJSON *usage = cJSON_CreateObject();
        cJSON_AddNumberToObject(usage, "prompt_tokens", collect.input_tokens);
        cJSON_AddNumberToObject(usage, "completion_tokens", collect.output_tokens);
        cJSON_AddNumberToObject(usage, "total_tokens",
                                collect.input_tokens + collect.output_tokens);
        cJSON_AddItemToObject(resp_json, "usage", usage);
    } else {
        http_response_set_status(resp, 502, "Bad Gateway");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "message", "AI API request failed");
        cJSON_AddStringToObject(error, "type", "server_error");
        cJSON_AddItemToObject(resp_json, "error", error);
    }

    char *resp_body = cJSON_PrintUnformatted(resp_json);
    http_response_set_json(resp, resp_body);
    free(resp_body);
    cJSON_Delete(resp_json);
    cJSON_Delete(json);
    free(collect.buf);

    return OC_OK;
}
