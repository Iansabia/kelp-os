/*
 * stream.h — SSE stream parsers for AI providers
 */
#ifndef STREAM_H
#define STREAM_H

#include "openclaw.h"
#include <stddef.h>

/*
 * Initialize a stream context. Must be called before use.
 */
void stream_ctx_init(stream_ctx_t *ctx);

/*
 * Free internal buffers in a stream context.
 */
void stream_ctx_cleanup(stream_ctx_t *ctx);

/*
 * Feed raw SSE data from Anthropic API into the parser.
 * Calls ctx->on_text for each content delta.
 * Calls ctx->on_done when message_stop is received.
 */
int stream_anthropic_feed(stream_ctx_t *ctx, const char *data, size_t len);

/*
 * Feed raw SSE data from OpenAI API into the parser.
 * Calls ctx->on_text for each content delta.
 * Calls ctx->on_done when [DONE] is received.
 */
int stream_openai_feed(stream_ctx_t *ctx, const char *data, size_t len);

/*
 * Generic feed function that dispatches to the right parser
 * based on ctx->provider.
 */
int stream_feed(stream_ctx_t *ctx, const char *data, size_t len);

#endif /* STREAM_H */
