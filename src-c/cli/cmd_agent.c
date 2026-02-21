#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cli.h"
#include "config.h"
#include "auth.h"
#include "http.h"
#include "stream.h"
#include "json.h"
#include "openclaw.h"

/* API endpoints */
#define ANTHROPIC_API_URL "https://api.anthropic.com/v1/messages"
#define OPENAI_API_URL    "https://api.openai.com/v1/chat/completions"

/* Streaming callbacks */
static void print_chunk(const char *text, size_t len, void *userdata) {
    (void)userdata;
    fwrite(text, 1, len, stdout);
    fflush(stdout);
}

static void print_usage(int input_tokens, int output_tokens, void *userdata) {
    (void)userdata;
    fprintf(stderr, "\n[tokens: %d in, %d out]\n", input_tokens, output_tokens);
}

static void print_error(const char *error, void *userdata) {
    (void)userdata;
    fprintf(stderr, "\n[error: %s]\n", error);
}

/* Read message from stdin if not provided via -m */
static char *read_stdin_message(void) {
    if (isatty(STDIN_FILENO)) return NULL;

    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    size_t nread;
    while ((nread = fread(buf + len, 1, cap - len - 1, stdin)) > 0) {
        len += nread;
        if (len + 1 >= cap) {
            cap *= 2;
            char *new_buf = realloc(buf, cap);
            if (!new_buf) { free(buf); return NULL; }
            buf = new_buf;
        }
    }
    buf[len] = '\0';
    return (len > 0) ? buf : (free(buf), NULL);
}

int cmd_agent(int argc, char **argv) {
    agent_opts_t opts = {0};
    opts.max_tokens = -1;  /* Use config default */
    opts.temperature = -1; /* Use config default */

    if (parse_agent_opts(argc, argv, &opts) != OC_OK) {
        return 1;
    }

    /* Read from stdin if no -m flag */
    char *stdin_msg = NULL;
    if (!opts.message) {
        stdin_msg = read_stdin_message();
        if (!stdin_msg) {
            fprintf(stderr, "Error: No message provided. Use -m \"message\" or pipe input.\n");
            return 1;
        }
        opts.message = stdin_msg;
    }

    if (opts.verbose) {
        g_log_level = LOG_DEBUG;
    }

    /* Load config */
    config_t *cfg = config_load(NULL);
    if (!cfg) {
        oc_error("Failed to load config");
        free(stdin_msg);
        return 1;
    }

    /* Determine provider */
    provider_t provider;
    if (opts.provider) {
        provider = auth_parse_provider(opts.provider);
    } else {
        provider = auth_parse_provider(cfg->default_provider);
    }

    /* Resolve API key */
    const char *api_key = auth_resolve(cfg, provider);
    if (!api_key) {
        config_free(cfg);
        free(stdin_msg);
        return 1;
    }

    /* Determine model */
    const char *model = opts.model;
    if (!model) {
        model = (provider == PROVIDER_ANTHROPIC) ? cfg->anthropic_model : cfg->openai_model;
    }

    /* Determine settings */
    int max_tokens = (opts.max_tokens > 0) ? opts.max_tokens : cfg->max_tokens;
    double temperature = (opts.temperature >= 0) ? opts.temperature : cfg->temperature;
    const char *system_prompt = opts.system_prompt ? opts.system_prompt : cfg->system_prompt;

    /* Build request */
    const char *url;
    cJSON *body;
    if (provider == PROVIDER_ANTHROPIC) {
        url = ANTHROPIC_API_URL;
        body = json_build_anthropic_request(model, system_prompt,
                                             opts.message, max_tokens, temperature);
    } else {
        url = OPENAI_API_URL;
        body = json_build_openai_request(model, system_prompt,
                                          opts.message, max_tokens, temperature);
    }

    if (!body) {
        oc_error("Failed to build request body");
        config_free(cfg);
        free(stdin_msg);
        return 1;
    }

    oc_debug("Provider: %s, Model: %s, Max tokens: %d",
             auth_provider_name(provider), model, max_tokens);

    /* Initialize HTTP */
    if (http_init() != OC_OK) {
        oc_error("Failed to initialize HTTP");
        cJSON_Delete(body);
        config_free(cfg);
        free(stdin_msg);
        return 1;
    }

    /* Stream response */
    stream_ctx_t ctx = {
        .provider = provider,
        .on_text  = print_chunk,
        .on_done  = print_usage,
        .on_error = print_error,
        .userdata = NULL,
    };
    stream_ctx_init(&ctx);

    int rc = http_stream_post(url, api_key, body, &ctx, provider);

    if (rc == OC_OK) {
        /* Print final newline if output didn't end with one */
        printf("\n");
    }

    /* Cleanup */
    stream_ctx_cleanup(&ctx);
    cJSON_Delete(body);
    http_cleanup();
    config_free(cfg);
    free(stdin_msg);

    return (rc == OC_OK) ? 0 : 1;
}
