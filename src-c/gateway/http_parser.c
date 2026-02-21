#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gateway.h"
#include "openclaw.h"

connection_t *connection_create(int fd) {
    connection_t *conn = calloc(1, sizeof(connection_t));
    if (!conn) return NULL;

    conn->fd = fd;
    conn->state = CONN_READING_HEADERS;
    conn->read_cap = GW_READ_BUF_SIZE;
    conn->read_buf = malloc(conn->read_cap);
    if (!conn->read_buf) {
        free(conn);
        return NULL;
    }
    conn->read_len = 0;
    conn->keep_alive = true;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    conn->connected_at = (uint64_t)ts.tv_sec;

    return conn;
}

void connection_destroy(connection_t *conn) {
    if (!conn) return;
    free(conn->read_buf);
    free(conn->write_buf);
    free(conn->request.body);
    free(conn->ws_session_id);
    free(conn);
}

int connection_read(connection_t *conn) {
    while (1) {
        if (conn->read_len + 1 >= conn->read_cap) {
            size_t new_cap = conn->read_cap * 2;
            if (new_cap > GW_MAX_BODY_LEN + GW_READ_BUF_SIZE) {
                oc_error("Request too large");
                return -1;
            }
            char *new_buf = realloc(conn->read_buf, new_cap);
            if (!new_buf) return -1;
            conn->read_buf = new_buf;
            conn->read_cap = new_cap;
        }

        ssize_t n = read(conn->fd, conn->read_buf + conn->read_len,
                         conn->read_cap - conn->read_len - 1);
        if (n > 0) {
            conn->read_len += (size_t)n;
            conn->read_buf[conn->read_len] = '\0';
        } else if (n == 0) {
            return -1; /* Connection closed */
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
    }
}

static http_method_t parse_method(const char *s, size_t len) {
    if (len == 3 && memcmp(s, "GET", 3) == 0) return HTTP_GET;
    if (len == 4 && memcmp(s, "POST", 4) == 0) return HTTP_POST;
    if (len == 3 && memcmp(s, "PUT", 3) == 0) return HTTP_PUT;
    if (len == 6 && memcmp(s, "DELETE", 6) == 0) return HTTP_DELETE;
    if (len == 7 && memcmp(s, "OPTIONS", 7) == 0) return HTTP_OPTIONS;
    if (len == 4 && memcmp(s, "HEAD", 4) == 0) return HTTP_HEAD;
    return HTTP_UNKNOWN;
}

static const char *find_header(http_request_t *req, const char *key) {
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, key) == 0) {
            return req->headers[i].value;
        }
    }
    return NULL;
}

/*
 * Parse HTTP request from connection buffer.
 * Returns: 0 = complete, 1 = need more data, -1 = error
 */
int http_parse_request(connection_t *conn) {
    http_request_t *req = &conn->request;

    if (conn->state == CONN_READING_HEADERS) {
        /* Find end of headers */
        char *header_end = strstr(conn->read_buf, "\r\n\r\n");
        if (!header_end) return 1; /* Need more data */

        /* Parse request line */
        char *line = conn->read_buf;
        char *space1 = strchr(line, ' ');
        if (!space1) return -1;

        req->method = parse_method(line, (size_t)(space1 - line));

        char *url_start = space1 + 1;
        char *space2 = strchr(url_start, ' ');
        if (!space2) return -1;

        size_t url_len = (size_t)(space2 - url_start);
        if (url_len >= GW_MAX_URL_LEN) return -1;
        memcpy(req->url, url_start, url_len);
        req->url[url_len] = '\0';

        /* Split URL into path and query */
        char *qmark = strchr(req->url, '?');
        if (qmark) {
            size_t path_len = (size_t)(qmark - req->url);
            memcpy(req->path, req->url, path_len);
            req->path[path_len] = '\0';
            strncpy(req->query, qmark + 1, GW_MAX_URL_LEN - 1);
        } else {
            strncpy(req->path, req->url, GW_MAX_URL_LEN - 1);
            req->query[0] = '\0';
        }

        /* Parse HTTP version */
        req->version_major = 1;
        req->version_minor = 1;

        /* Parse headers */
        char *hdr_line = strstr(line, "\r\n");
        if (hdr_line) hdr_line += 2;

        req->header_count = 0;
        while (hdr_line && hdr_line < header_end && req->header_count < GW_MAX_HEADERS) {
            char *next_line = strstr(hdr_line, "\r\n");
            if (!next_line || next_line == hdr_line) break;

            char *colon = strchr(hdr_line, ':');
            if (colon && colon < next_line) {
                size_t key_len = (size_t)(colon - hdr_line);
                if (key_len < 256) {
                    memcpy(req->headers[req->header_count].key, hdr_line, key_len);
                    req->headers[req->header_count].key[key_len] = '\0';
                }

                char *val_start = colon + 1;
                while (*val_start == ' ') val_start++;
                size_t val_len = (size_t)(next_line - val_start);
                if (val_len < 512) {
                    memcpy(req->headers[req->header_count].value, val_start, val_len);
                    req->headers[req->header_count].value[val_len] = '\0';
                }
                req->header_count++;
            }
            hdr_line = next_line + 2;
        }

        /* Check Content-Length */
        const char *cl = find_header(req, "Content-Length");
        if (cl) {
            req->content_length = (size_t)atol(cl);
        }

        /* Check Connection header for keep-alive */
        const char *conn_hdr = find_header(req, "Connection");
        if (conn_hdr && strcasecmp(conn_hdr, "close") == 0) {
            conn->keep_alive = false;
        }

        size_t header_size = (size_t)(header_end - conn->read_buf) + 4;

        if (req->content_length > 0) {
            conn->state = CONN_READING_BODY;
            /* Check if body is already in buffer */
            size_t body_available = conn->read_len - header_size;
            if (body_available >= req->content_length) {
                req->body = malloc(req->content_length + 1);
                if (!req->body) return -1;
                memcpy(req->body, conn->read_buf + header_size, req->content_length);
                req->body[req->content_length] = '\0';
                req->body_len = req->content_length;
                return 0; /* Complete */
            }
            return 1; /* Need more body data */
        }

        return 0; /* Complete, no body */
    }

    if (conn->state == CONN_READING_BODY) {
        char *header_end = strstr(conn->read_buf, "\r\n\r\n");
        if (!header_end) return -1;
        size_t header_size = (size_t)(header_end - conn->read_buf) + 4;
        size_t body_available = conn->read_len - header_size;

        if (body_available >= req->content_length) {
            req->body = malloc(req->content_length + 1);
            if (!req->body) return -1;
            memcpy(req->body, conn->read_buf + header_size, req->content_length);
            req->body[req->content_length] = '\0';
            req->body_len = req->content_length;
            return 0;
        }
        return 1;
    }

    return -1;
}
