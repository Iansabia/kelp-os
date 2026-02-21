#include <stdio.h>
#include <string.h>
#include "gateway.h"
#include "openclaw.h"

/* Check if a request has a valid Bearer token */
int auth_gateway_check(http_request_t *req, const char *expected_token) {
    if (!expected_token || !*expected_token) {
        return OC_OK; /* No auth configured = allow all */
    }

    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, "Authorization") == 0) {
            const char *val = req->headers[i].value;
            if (strncmp(val, "Bearer ", 7) == 0) {
                if (strcmp(val + 7, expected_token) == 0) {
                    return OC_OK;
                }
            }
        }
    }

    oc_warn("Unauthorized request to %s", req->path);
    return OC_ERR_AUTH;
}
