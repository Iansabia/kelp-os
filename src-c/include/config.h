/*
 * config.h — Configuration loading (JSON + env vars)
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "openclaw.h"
#include "cJSON.h"

typedef struct {
    /* AI provider settings */
    char       *default_provider;     /* "anthropic" or "openai" */
    char       *anthropic_model;      /* e.g. "claude-sonnet-4-20250514" */
    char       *openai_model;         /* e.g. "gpt-4o" */
    int         max_tokens;
    double      temperature;

    /* API keys (resolved from env or config) */
    char       *anthropic_api_key;
    char       *openai_api_key;

    /* Gateway settings */
    int         gateway_port;
    char       *gateway_bind;
    char       *tls_cert_path;
    char       *tls_key_path;
    char       *auth_token;           /* Bearer token for gateway */

    /* Paths */
    char       *config_dir;           /* ~/.openclaw */
    char       *session_db_path;      /* ~/.openclaw/sessions.db */
    char       *log_path;

    /* System prompt */
    char       *system_prompt;

    /* Raw JSON for extensions */
    cJSON      *raw;
} config_t;

/*
 * Load config from path. If path is NULL, uses ~/.openclaw/openclaw.json.
 * Environment variables override file values.
 * Returns NULL on error.
 */
config_t *config_load(const char *path);

/*
 * Free all config memory.
 */
void config_free(config_t *cfg);

/*
 * Get a string value from config, with env var override.
 * Checks env_var first, then JSON path, then returns default_val.
 */
const char *config_get_str(config_t *cfg, const char *json_path,
                           const char *env_var, const char *default_val);

/*
 * Get an integer value from config.
 */
int config_get_int(config_t *cfg, const char *json_path,
                   const char *env_var, int default_val);

#endif /* CONFIG_H */
