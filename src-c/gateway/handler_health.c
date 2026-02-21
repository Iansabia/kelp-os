#include <stdio.h>
#include <string.h>
#include <time.h>
#include "gateway.h"
#include "openclaw.h"
#include "cJSON.h"

int handler_health(connection_t *conn, http_request_t *req,
                   http_response_build_t *resp, void *ctx) {
    (void)conn; (void)req;
    gateway_ctx_t *gw = (gateway_ctx_t *)ctx;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t uptime = (uint64_t)ts.tv_sec - gw->start_time;

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "ok");
    cJSON_AddStringToObject(json, "version", OPENCLAW_VERSION);
    cJSON_AddNumberToObject(json, "uptime_seconds", (double)uptime);
    cJSON_AddNumberToObject(json, "total_requests", (double)gw->total_requests);
    cJSON_AddNumberToObject(json, "active_connections", (double)gw->active_connections);

    char *body = cJSON_PrintUnformatted(json);
    http_response_set_json(resp, body);
    free(body);
    cJSON_Delete(json);

    return OC_OK;
}
