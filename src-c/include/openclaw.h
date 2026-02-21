/*
 * openclaw.h — Common types and constants for OpenClaw
 */
#ifndef OPENCLAW_H
#define OPENCLAW_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define OPENCLAW_VERSION_MAJOR 0
#define OPENCLAW_VERSION_MINOR 1
#define OPENCLAW_VERSION_PATCH 0
#define OPENCLAW_VERSION "0.1.0"

#define OPENCLAW_DEFAULT_PORT       18789
#define OPENCLAW_MAX_MESSAGE_LEN    (1024 * 1024)  /* 1 MiB */
#define OPENCLAW_MAX_TOKENS_DEFAULT 4096
#define OPENCLAW_SESSION_KEY_LEN    32

/* Provider IDs */
typedef enum {
    PROVIDER_ANTHROPIC = 0,
    PROVIDER_OPENAI    = 1,
    PROVIDER_COUNT
} provider_t;

/* Log levels */
typedef enum {
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

/* Message role */
typedef enum {
    ROLE_USER = 0,
    ROLE_ASSISTANT,
    ROLE_SYSTEM
} role_t;

/* A single message in a conversation */
typedef struct {
    role_t      role;
    char       *content;
    uint64_t    timestamp;
} message_t;

/* Stream callback types */
typedef void (*stream_text_cb)(const char *text, size_t len, void *userdata);
typedef void (*stream_done_cb)(int input_tokens, int output_tokens, void *userdata);
typedef void (*stream_error_cb)(const char *error, void *userdata);

/* Stream context passed to HTTP streaming */
typedef struct {
    provider_t       provider;
    stream_text_cb   on_text;
    stream_done_cb   on_done;
    stream_error_cb  on_error;
    void            *userdata;
    /* Internal parser state */
    char            *buf;
    size_t           buf_len;
    size_t           buf_cap;
    int              input_tokens;
    int              output_tokens;
} stream_ctx_t;

/* Return codes */
#define OC_OK           0
#define OC_ERR         -1
#define OC_ERR_CONFIG  -2
#define OC_ERR_AUTH    -3
#define OC_ERR_HTTP    -4
#define OC_ERR_PARSE   -5
#define OC_ERR_IO      -6

/* Logging macros */
extern log_level_t g_log_level;

#define oc_log(level, fmt, ...) do { \
    if ((level) >= g_log_level) { \
        const char *_names[] = {"TRACE","DEBUG","INFO","WARN","ERROR","FATAL"}; \
        fprintf(stderr, "[%s] " fmt "\n", _names[(level)], ##__VA_ARGS__); \
    } \
} while(0)

#define oc_trace(fmt, ...) oc_log(LOG_TRACE, fmt, ##__VA_ARGS__)
#define oc_debug(fmt, ...) oc_log(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define oc_info(fmt, ...)  oc_log(LOG_INFO,  fmt, ##__VA_ARGS__)
#define oc_warn(fmt, ...)  oc_log(LOG_WARN,  fmt, ##__VA_ARGS__)
#define oc_error(fmt, ...) oc_log(LOG_ERROR, fmt, ##__VA_ARGS__)
#define oc_fatal(fmt, ...) oc_log(LOG_FATAL, fmt, ##__VA_ARGS__)

#endif /* OPENCLAW_H */
