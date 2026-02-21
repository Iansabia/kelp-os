#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "openclaw.h"

/*
 * Minimal sd_notify implementation that doesn't require libsystemd.
 * Sends notification messages to the systemd notify socket.
 */

static int sd_notify_send(const char *state) {
    const char *notify_socket = getenv("NOTIFY_SOCKET");
    if (!notify_socket) return 0; /* Not running under systemd */

    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, notify_socket, sizeof(addr.sun_path) - 1);

    /* Handle abstract socket names */
    if (addr.sun_path[0] == '@') {
        addr.sun_path[0] = '\0';
    }

    socklen_t addr_len = offsetof(struct sockaddr_un, sun_path) + strlen(notify_socket);

    ssize_t n = sendto(fd, state, strlen(state), 0,
                       (struct sockaddr *)&addr, addr_len);
    close(fd);

    return (n > 0) ? 0 : -1;
}

int systemd_notify_ready(void) {
    oc_debug("Notifying systemd: READY=1");
    return sd_notify_send("READY=1");
}

int systemd_notify_stopping(void) {
    oc_debug("Notifying systemd: STOPPING=1");
    return sd_notify_send("STOPPING=1");
}

int systemd_notify_status(const char *status) {
    char buf[256];
    snprintf(buf, sizeof(buf), "STATUS=%s", status);
    return sd_notify_send(buf);
}

int systemd_notify_watchdog(void) {
    return sd_notify_send("WATCHDOG=1");
}
