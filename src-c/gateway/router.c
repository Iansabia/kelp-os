#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gateway.h"
#include "openclaw.h"

gateway_ctx_t *gateway_create(config_t *cfg) {
    gateway_ctx_t *gw = calloc(1, sizeof(gateway_ctx_t));
    if (!gw) return NULL;

    gw->cfg = cfg;
    gw->route_cap = 32;
    gw->routes = calloc((size_t)gw->route_cap, sizeof(route_t));
    if (!gw->routes) { free(gw); return NULL; }

    gw->max_fd = 1024;
    gw->connections = calloc((size_t)gw->max_fd, sizeof(connection_t *));
    if (!gw->connections) { free(gw->routes); free(gw); return NULL; }

    pthread_mutex_init(&gw->lock, NULL);
    gw->running = false;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    gw->start_time = (uint64_t)ts.tv_sec;

    return gw;
}

int gateway_add_route(gateway_ctx_t *gw, http_method_t method,
                      const char *pattern, route_handler_t handler, void *ctx) {
    if (gw->route_count >= gw->route_cap) {
        gw->route_cap *= 2;
        route_t *new_routes = realloc(gw->routes, (size_t)gw->route_cap * sizeof(route_t));
        if (!new_routes) return OC_ERR;
        gw->routes = new_routes;
    }

    gw->routes[gw->route_count] = (route_t){
        .method = method,
        .pattern = pattern,
        .handler = handler,
        .ctx = ctx,
    };
    gw->route_count++;
    oc_debug("Route registered: %s %s",
             method == HTTP_GET ? "GET" : "POST", pattern);
    return OC_OK;
}

static bool route_matches(const route_t *route, http_method_t method, const char *path) {
    if (route->method != method) return false;

    const char *pattern = route->pattern;
    size_t plen = strlen(pattern);

    /* Exact match */
    if (strcmp(pattern, path) == 0) return true;

    /* Wildcard: /v1/* matches /v1/anything */
    if (plen > 1 && pattern[plen - 1] == '*') {
        if (strncmp(pattern, path, plen - 1) == 0) return true;
    }

    return false;
}

int router_dispatch(gateway_ctx_t *gw, connection_t *conn) {
    http_request_t *req = &conn->request;
    http_response_build_t resp;
    http_response_init(&resp);

    /* CORS preflight */
    if (req->method == HTTP_OPTIONS) {
        http_response_set_status(&resp, 204, "No Content");
        http_response_add_header(&resp, "Access-Control-Allow-Origin", "*");
        http_response_add_header(&resp, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        http_response_add_header(&resp, "Access-Control-Allow-Headers", "Content-Type, Authorization");
        http_response_send(conn, &resp);
        http_response_cleanup(&resp);
        return OC_OK;
    }

    /* Find matching route */
    for (int i = 0; i < gw->route_count; i++) {
        if (route_matches(&gw->routes[i], req->method, req->path)) {
            http_response_add_header(&resp, "Access-Control-Allow-Origin", "*");
            int rc = gw->routes[i].handler(conn, req, &resp, gw->routes[i].ctx);
            http_response_send(conn, &resp);
            http_response_cleanup(&resp);
            return rc;
        }
    }

    /* 404 Not Found */
    http_response_set_status(&resp, 404, "Not Found");
    http_response_set_json(&resp, "{\"error\":\"Not Found\"}");
    http_response_send(conn, &resp);
    http_response_cleanup(&resp);
    return OC_OK;
}

int gateway_start(gateway_ctx_t *gw) {
    /* Create listening socket */
    gw->listen_fd = epoll_server_listen(gw->cfg->gateway_bind,
                                         gw->cfg->gateway_port, 128);
    if (gw->listen_fd < 0) return OC_ERR;

    /* Create epoll instance */
    gw->epoll_fd = epoll_server_create(gw->listen_fd);
    if (gw->epoll_fd < 0) {
        close(gw->listen_fd);
        return OC_ERR;
    }

    /* Run event loop */
    return epoll_server_run(gw);
}

void gateway_stop(gateway_ctx_t *gw) {
    gw->running = false;
}

void gateway_destroy(gateway_ctx_t *gw) {
    if (!gw) return;

    /* Close all connections */
    for (int i = 0; i < gw->max_fd; i++) {
        if (gw->connections[i]) {
            connection_destroy(gw->connections[i]);
            close(i);
        }
    }

    if (gw->listen_fd >= 0) close(gw->listen_fd);
    if (gw->epoll_fd >= 0) close(gw->epoll_fd);

    free(gw->connections);
    free(gw->routes);
    free(gw->channels);
    pthread_mutex_destroy(&gw->lock);
    free(gw);
}
