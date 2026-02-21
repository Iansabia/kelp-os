/*
 * http.h — HTTP client (libcurl wrapper)
 */
#ifndef HTTP_H
#define HTTP_H

#include "openclaw.h"
#include "cJSON.h"

/* HTTP response for non-streaming requests */
typedef struct {
    int         status_code;
    char       *body;
    size_t      body_len;
    char       *content_type;
} http_response_t;

/*
 * Initialize the HTTP subsystem (call once at startup).
 */
int http_init(void);

/*
 * Cleanup the HTTP subsystem (call once at shutdown).
 */
void http_cleanup(void);

/*
 * Perform a streaming POST request.
 *
 * url      — Full URL (e.g. "https://api.anthropic.com/v1/messages")
 * api_key  — API key for Authorization header
 * body     — JSON body to POST
 * ctx      — Stream context with callbacks
 * provider — Provider enum (affects headers)
 *
 * Returns OC_OK on success.
 */
int http_stream_post(const char *url, const char *api_key,
                     cJSON *body, stream_ctx_t *ctx, provider_t provider);

/*
 * Perform a blocking POST request.
 * Caller must free response with http_response_free().
 */
int http_post(const char *url, const char *api_key,
              cJSON *body, http_response_t *resp, provider_t provider);

/*
 * Free an HTTP response.
 */
void http_response_free(http_response_t *resp);

#endif /* HTTP_H */
