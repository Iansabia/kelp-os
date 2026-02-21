#include <stdio.h>
#include <string.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "gateway.h"
#include "openclaw.h"

typedef struct {
    SSL_CTX *ctx;
} tls_ctx_t;

tls_ctx_t *tls_init(const char *cert_path, const char *key_path) {
    if (!cert_path || !key_path) return NULL;

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ssl_ctx = SSL_CTX_new(method);
    if (!ssl_ctx) {
        oc_error("Failed to create SSL context");
        return NULL;
    }

    SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);

    if (SSL_CTX_use_certificate_file(ssl_ctx, cert_path, SSL_FILETYPE_PEM) <= 0) {
        oc_error("Failed to load TLS certificate: %s", cert_path);
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    if (SSL_CTX_use_PrivateKey_file(ssl_ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
        oc_error("Failed to load TLS private key: %s", key_path);
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    if (!SSL_CTX_check_private_key(ssl_ctx)) {
        oc_error("TLS certificate and private key don't match");
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    tls_ctx_t *tls = malloc(sizeof(tls_ctx_t));
    if (!tls) {
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }
    tls->ctx = ssl_ctx;

    oc_info("TLS initialized with cert: %s", cert_path);
    return tls;
}

void tls_cleanup(tls_ctx_t *tls) {
    if (!tls) return;
    SSL_CTX_free(tls->ctx);
    free(tls);
}
