#ifndef CHANNEL_H
#define CHANNEL_H

#include "gateway.h"

/* Built-in channel plugins */
extern const channel_plugin_t channel_webchat;

/* Initialize all built-in channels */
int channels_init(gateway_ctx_t *gw);

/* Shutdown all channels */
void channels_shutdown(gateway_ctx_t *gw);

#endif /* CHANNEL_H */
