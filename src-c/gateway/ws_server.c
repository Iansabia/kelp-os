#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include "gateway.h"
#include "openclaw.h"

#define WS_MAGIC "258EAFA5-E914-47DA-95CA-5AB5DC085B11"

/* Base64 encode using OpenSSL */
static char *base64_encode(const unsigned char *input, int length) {
    BIO *bmem, *b64;
    BUF_MEM *bptr;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, input, length);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    char *buf = malloc(bptr->length + 1);
    if (buf) {
        memcpy(buf, bptr->data, bptr->length);
        buf[bptr->length] = '\0';
    }

    BIO_free_all(b64);
    return buf;
}

/* Find header in request */
static const char *ws_find_header(http_request_t *req, const char *key) {
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, key) == 0)
            return req->headers[i].value;
    }
    return NULL;
}

int ws_handle_upgrade(connection_t *conn, http_request_t *req) {
    const char *ws_key = ws_find_header(req, "Sec-WebSocket-Key");
    if (!ws_key) return -1;

    /* Generate accept key: SHA1(key + magic) → base64 */
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", ws_key, WS_MAGIC);

    unsigned char sha1[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)combined, strlen(combined), sha1);

    char *accept_key = base64_encode(sha1, SHA_DIGEST_LENGTH);
    if (!accept_key) return -1;

    /* Send upgrade response */
    char response[512];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept_key);
    free(accept_key);

    ssize_t n = write(conn->fd, response, (size_t)len);
    if (n != len) return -1;

    conn->is_websocket = true;
    conn->state = CONN_WEBSOCKET;
    return 0;
}

int ws_send_text(connection_t *conn, const char *text, size_t len) {
    if (!conn->is_websocket) return -1;

    /* Build WebSocket frame */
    unsigned char header[10];
    size_t header_len = 0;

    header[0] = 0x81;  /* FIN + text opcode */
    header_len = 1;

    if (len < 126) {
        header[1] = (unsigned char)len;
        header_len = 2;
    } else if (len < 65536) {
        header[1] = 126;
        header[2] = (unsigned char)((len >> 8) & 0xFF);
        header[3] = (unsigned char)(len & 0xFF);
        header_len = 4;
    } else {
        header[1] = 127;
        for (int i = 0; i < 8; i++) {
            header[9 - i] = (unsigned char)((len >> (8 * i)) & 0xFF);
        }
        header_len = 10;
    }

    /* Write header + payload */
    ssize_t n = write(conn->fd, header, header_len);
    if (n != (ssize_t)header_len) return -1;

    size_t written = 0;
    while (written < len) {
        n = write(conn->fd, text + written, len - written);
        if (n <= 0) return -1;
        written += (size_t)n;
    }

    return 0;
}

int ws_read_frame(connection_t *conn, char **payload, size_t *payload_len) {
    unsigned char header[2];
    ssize_t n = read(conn->fd, header, 2);
    if (n != 2) return -1;

    /* bool fin = (header[0] & 0x80) != 0; */
    int opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    size_t len = header[1] & 0x7F;

    if (opcode == 0x08) return -1;  /* Close frame */

    if (len == 126) {
        unsigned char ext[2];
        if (read(conn->fd, ext, 2) != 2) return -1;
        len = ((size_t)ext[0] << 8) | ext[1];
    } else if (len == 127) {
        unsigned char ext[8];
        if (read(conn->fd, ext, 8) != 8) return -1;
        len = 0;
        for (int i = 0; i < 8; i++) {
            len = (len << 8) | ext[i];
        }
    }

    unsigned char mask_key[4] = {0};
    if (masked) {
        if (read(conn->fd, mask_key, 4) != 4) return -1;
    }

    char *data = malloc(len + 1);
    if (!data) return -1;

    size_t total_read = 0;
    while (total_read < len) {
        n = read(conn->fd, data + total_read, len - total_read);
        if (n <= 0) { free(data); return -1; }
        total_read += (size_t)n;
    }

    if (masked) {
        for (size_t i = 0; i < len; i++) {
            data[i] ^= (char)mask_key[i % 4];
        }
    }

    data[len] = '\0';
    *payload = data;
    *payload_len = len;
    return opcode;
}
