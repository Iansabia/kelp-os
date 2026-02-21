#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gateway.h"
#include "channel.h"
#include "openclaw.h"

static int webchat_init(channel_t *ch, config_t *cfg) {
    (void)cfg;
    oc_info("Web chat channel initialized");
    ch->priv = NULL;
    return OC_OK;
}

static int webchat_on_message(channel_t *ch, const char *session_id,
                               const char *text, reply_fn_t reply) {
    (void)ch;
    oc_debug("Webchat message [%s]: %.100s", session_id, text);
    /* The actual AI call is handled by the webhook handler.
     * This is for when we add async message processing. */
    if (reply) {
        reply(ch, session_id, "Message received");
    }
    return OC_OK;
}

static void webchat_shutdown(channel_t *ch) {
    (void)ch;
    oc_info("Web chat channel shut down");
}

const channel_plugin_t channel_webchat = {
    .id         = "webchat",
    .init       = webchat_init,
    .on_message = webchat_on_message,
    .shutdown   = webchat_shutdown,
};

int channels_init(gateway_ctx_t *gw) {
    /* Allocate channels array */
    gw->channel_count = 1; /* Just webchat for now */
    gw->channels = calloc((size_t)gw->channel_count, sizeof(channel_t));
    if (!gw->channels) return OC_ERR;

    /* Initialize webchat channel */
    gw->channels[0].plugin = &channel_webchat;
    gw->channels[0].cfg = gw->cfg;
    return gw->channels[0].plugin->init(&gw->channels[0], gw->cfg);
}

void channels_shutdown(gateway_ctx_t *gw) {
    for (int i = 0; i < gw->channel_count; i++) {
        if (gw->channels[i].plugin && gw->channels[i].plugin->shutdown) {
            gw->channels[i].plugin->shutdown(&gw->channels[i]);
        }
    }
}
