#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gateway.h"
#include "openclaw.h"

void http_response_init(http_response_build_t *resp) {
    memset(resp, 0, sizeof(*resp));
    resp->status_code = 200;
    resp->status_text = "OK";
}

void http_response_set_status(http_response_build_t *resp, int code, const char *text) {
    resp->status_code = code;
    resp->status_text = text;
}

void http_response_add_header(http_response_build_t *resp, const char *key, const char *value) {
    size_t needed = strlen(key) + strlen(value) + 4; /* ": " + "\r\n" */
    size_t new_len = resp->headers_len + needed;

    if (new_len >= resp->headers_cap) {
        size_t new_cap = resp->headers_cap == 0 ? 512 : resp->headers_cap * 2;
        while (new_cap < new_len + 1) new_cap *= 2;
        char *new_buf = realloc(resp->headers_buf, new_cap);
        if (!new_buf) return;
        resp->headers_buf = new_buf;
        resp->headers_cap = new_cap;
    }

    resp->headers_len += (size_t)snprintf(resp->headers_buf + resp->headers_len,
                                           resp->headers_cap - resp->headers_len,
                                           "%s: %s\r\n", key, value);
}

void http_response_set_body(http_response_build_t *resp, const char *body, size_t len) {
    free(resp->body);
    resp->body = malloc(len + 1);
    if (resp->body) {
        memcpy(resp->body, body, len);
        resp->body[len] = '\0';
        resp->body_len = len;
    }
}

void http_response_set_json(http_response_build_t *resp, const char *json) {
    http_response_add_header(resp, "Content-Type", "application/json");
    http_response_set_body(resp, json, strlen(json));
}

int http_response_send(connection_t *conn, http_response_build_t *resp) {
    /* Build status line */
    char status_line[128];
    int sl_len = snprintf(status_line, sizeof(status_line),
                          "HTTP/1.1 %d %s\r\n", resp->status_code, resp->status_text);

    /* Content-Length header */
    char cl_header[64];
    int cl_len = snprintf(cl_header, sizeof(cl_header),
                          "Content-Length: %zu\r\n", resp->body_len);

    /* Calculate total response size */
    size_t total = (size_t)sl_len + resp->headers_len + (size_t)cl_len + 2; /* \r\n */
    if (resp->body_len > 0) total += resp->body_len;

    char *buf = malloc(total + 1);
    if (!buf) return OC_ERR;

    size_t pos = 0;
    memcpy(buf + pos, status_line, (size_t)sl_len); pos += (size_t)sl_len;
    if (resp->headers_len > 0) {
        memcpy(buf + pos, resp->headers_buf, resp->headers_len);
        pos += resp->headers_len;
    }
    memcpy(buf + pos, cl_header, (size_t)cl_len); pos += (size_t)cl_len;
    memcpy(buf + pos, "\r\n", 2); pos += 2;
    if (resp->body_len > 0) {
        memcpy(buf + pos, resp->body, resp->body_len);
        pos += resp->body_len;
    }

    /* Write response */
    size_t written = 0;
    while (written < pos) {
        ssize_t n = write(conn->fd, buf + written, pos - written);
        if (n <= 0) {
            free(buf);
            return OC_ERR_IO;
        }
        written += (size_t)n;
    }

    free(buf);
    return OC_OK;
}

void http_response_cleanup(http_response_build_t *resp) {
    free(resp->headers_buf);
    free(resp->body);
    memset(resp, 0, sizeof(*resp));
}
