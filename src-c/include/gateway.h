/*
 * gateway.h — Gateway daemon types and interfaces
 */
#ifndef GATEWAY_H
#define GATEWAY_H

#include "openclaw.h"
#include "config.h"
#include <pthread.h>

#define GW_MAX_CONNECTIONS   1024
#define GW_READ_BUF_SIZE    8192
#define GW_MAX_HEADERS      64
#define GW_MAX_URL_LEN      2048
#define GW_MAX_BODY_LEN     (1024 * 1024)

/* HTTP method enum */
typedef enum {
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_OPTIONS,
    HTTP_HEAD,
    HTTP_UNKNOWN
} http_method_t;

/* Parsed HTTP request */
typedef struct {
    http_method_t   method;
    char            url[GW_MAX_URL_LEN];
    char            path[GW_MAX_URL_LEN];
    char            query[GW_MAX_URL_LEN];
    int             version_major;
    int             version_minor;
    struct {
        char key[256];
        char value[512];
    } headers[GW_MAX_HEADERS];
    int             header_count;
    char           *body;
    size_t          body_len;
    size_t          content_length;
} http_request_t;

/* HTTP response builder */
typedef struct {
    int             status_code;
    const char     *status_text;
    char           *headers_buf;
    size_t          headers_len;
    size_t          headers_cap;
    char           *body;
    size_t          body_len;
} http_response_build_t;

/* Connection state */
typedef enum {
    CONN_READING_HEADERS = 0,
    CONN_READING_BODY,
    CONN_PROCESSING,
    CONN_WRITING,
    CONN_WEBSOCKET,
    CONN_CLOSED
} conn_state_t;

/* Per-connection data */
typedef struct {
    int             fd;
    conn_state_t    state;
    char           *read_buf;
    size_t          read_len;
    size_t          read_cap;
    char           *write_buf;
    size_t          write_len;
    size_t          write_pos;
    http_request_t  request;
    uint64_t        connected_at;
    bool            keep_alive;
    /* WebSocket state */
    bool            is_websocket;
    char           *ws_session_id;
} connection_t;

/* Route handler function type */
typedef int (*route_handler_t)(connection_t *conn, http_request_t *req,
                                http_response_build_t *resp, void *ctx);

/* Route entry */
typedef struct {
    http_method_t   method;
    const char     *pattern;       /* e.g. "/hooks/webchat", "/v1/*", "/health" */
    route_handler_t handler;
    void           *ctx;
} route_t;

/* Channel plugin interface */
typedef struct channel channel_t;
typedef int (*reply_fn_t)(channel_t *ch, const char *session_id, const char *text);

typedef struct {
    const char *id;
    int  (*init)(channel_t *ch, config_t *cfg);
    int  (*on_message)(channel_t *ch, const char *session_id,
                       const char *text, reply_fn_t reply);
    void (*shutdown)(channel_t *ch);
} channel_plugin_t;

struct channel {
    const channel_plugin_t *plugin;
    config_t               *cfg;
    void                   *priv;  /* channel-private data */
};

/* Gateway context (global state) */
typedef struct {
    config_t       *cfg;
    int             listen_fd;
    int             epoll_fd;
    route_t        *routes;
    int             route_count;
    int             route_cap;
    connection_t  **connections;    /* indexed by fd */
    int             max_fd;
    channel_t      *channels;
    int             channel_count;
    pthread_mutex_t lock;
    volatile bool   running;
    /* Stats */
    uint64_t        total_requests;
    uint64_t        active_connections;
    uint64_t        start_time;
} gateway_ctx_t;

/* Gateway lifecycle */
gateway_ctx_t *gateway_create(config_t *cfg);
int  gateway_add_route(gateway_ctx_t *gw, http_method_t method,
                       const char *pattern, route_handler_t handler, void *ctx);
int  gateway_start(gateway_ctx_t *gw);
void gateway_stop(gateway_ctx_t *gw);
void gateway_destroy(gateway_ctx_t *gw);

/* Connection management */
connection_t *connection_create(int fd);
void connection_destroy(connection_t *conn);
int  connection_read(connection_t *conn);
int  connection_write(connection_t *conn);

/* HTTP parsing */
int  http_parse_request(connection_t *conn);
void http_response_init(http_response_build_t *resp);
void http_response_set_status(http_response_build_t *resp, int code, const char *text);
void http_response_add_header(http_response_build_t *resp, const char *key, const char *value);
void http_response_set_body(http_response_build_t *resp, const char *body, size_t len);
void http_response_set_json(http_response_build_t *resp, const char *json);
int  http_response_send(connection_t *conn, http_response_build_t *resp);
void http_response_cleanup(http_response_build_t *resp);

/* Routing */
int router_dispatch(gateway_ctx_t *gw, connection_t *conn);

/* Handlers */
int handler_health(connection_t *conn, http_request_t *req,
                   http_response_build_t *resp, void *ctx);
int handler_webhook(connection_t *conn, http_request_t *req,
                    http_response_build_t *resp, void *ctx);
int handler_chat(connection_t *conn, http_request_t *req,
                 http_response_build_t *resp, void *ctx);

/* WebSocket */
int ws_handle_upgrade(connection_t *conn, http_request_t *req);
int ws_send_text(connection_t *conn, const char *text, size_t len);
int ws_read_frame(connection_t *conn, char **payload, size_t *payload_len);

#endif /* GATEWAY_H */
