#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <time.h>
#include "gateway.h"
#include "epoll_server.h"
#include "session_store.h"
#include "channel.h"
#include "config.h"
#include "http.h"
#include "auth.h"
#include "openclaw.h"

static gateway_ctx_t *g_gateway = NULL;

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        oc_info("Received signal %d, shutting down...", sig);
        if (g_gateway) g_gateway->running = false;
    }
}

static void setup_signals(void) {
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);
}

static void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) exit(0);  /* Parent exits */

    if (setsid() < 0) exit(1);

    /* Second fork to prevent terminal acquisition */
    pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0);

    umask(0027);
    if (chdir("/") < 0) {
        /* non-fatal */
    }

    /* Redirect stdio to /dev/null */
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    /* Keep stderr for logging, or redirect to log file */
}

static void print_usage(void) {
    printf("Usage: openclawgw [options]\n\n");
    printf("Options:\n");
    printf("  --config PATH    Config file path\n");
    printf("  --port PORT      Listen port (default: 18789)\n");
    printf("  --bind ADDR      Bind address (default: 127.0.0.1)\n");
    printf("  --daemon         Run as daemon\n");
    printf("  --verbose        Verbose logging\n");
    printf("  --help           Show this help\n");
}

int main(int argc, char **argv) {
    const char *config_path = NULL;
    int port_override = 0;
    const char *bind_override = NULL;
    bool do_daemon = false;
    bool verbose = false;

    static struct option long_options[] = {
        {"config",  required_argument, 0, 'c'},
        {"port",    required_argument, 0, 'p'},
        {"bind",    required_argument, 0, 'b'},
        {"daemon",  no_argument,       0, 'd'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "c:p:b:dvh", long_options, NULL)) != -1) {
        switch (c) {
        case 'c': config_path = optarg; break;
        case 'p': port_override = atoi(optarg); break;
        case 'b': bind_override = optarg; break;
        case 'd': do_daemon = true; break;
        case 'v': verbose = true; break;
        case 'h': print_usage(); return 0;
        default:  print_usage(); return 1;
        }
    }

    if (verbose) g_log_level = LOG_DEBUG;

    /* Load config */
    config_t *cfg = config_load(config_path);
    if (!cfg) {
        oc_fatal("Failed to load configuration");
        return 1;
    }

    if (port_override > 0) cfg->gateway_port = port_override;
    if (bind_override) {
        free(cfg->gateway_bind);
        cfg->gateway_bind = strdup(bind_override);
    }

    /* Daemonize if requested */
    if (do_daemon) {
        oc_info("Daemonizing...");
        daemonize();
    }

    setup_signals();

    /* Init HTTP subsystem */
    if (http_init() != OC_OK) {
        oc_fatal("Failed to initialize HTTP subsystem");
        config_free(cfg);
        return 1;
    }

    /* Create gateway */
    gateway_ctx_t *gw = gateway_create(cfg);
    if (!gw) {
        oc_fatal("Failed to create gateway");
        http_cleanup();
        config_free(cfg);
        return 1;
    }
    g_gateway = gw;

    /* Register routes */
    gateway_add_route(gw, HTTP_GET,  "/health",         handler_health,  gw);
    gateway_add_route(gw, HTTP_POST, "/hooks/webchat",  handler_webhook, gw);
    gateway_add_route(gw, HTTP_POST, "/v1/chat/completions", handler_chat, gw);

    /* Initialize channels */
    channels_init(gw);

    oc_info("OpenClaw Gateway v%s starting on %s:%d",
            OPENCLAW_VERSION, cfg->gateway_bind, cfg->gateway_port);

    /* Start the server (blocks until shutdown) */
    int rc = gateway_start(gw);

    /* Cleanup */
    oc_info("Shutting down...");
    channels_shutdown(gw);
    gateway_destroy(gw);
    http_cleanup();
    config_free(cfg);

    oc_info("Goodbye.");
    return rc;
}
