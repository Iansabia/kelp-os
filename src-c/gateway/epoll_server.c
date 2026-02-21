#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <time.h>
#include "epoll_server.h"
#include "gateway.h"
#include "openclaw.h"

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int epoll_server_listen(const char *addr, int port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        oc_error("socket(): %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
    };
    if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) {
        oc_error("Invalid bind address: %s", addr);
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        oc_error("bind(%s:%d): %s", addr, port, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, backlog) < 0) {
        oc_error("listen(): %s", strerror(errno));
        close(fd);
        return -1;
    }

    set_nonblocking(fd);
    return fd;
}

int epoll_server_create(int listen_fd) {
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        oc_error("epoll_create1(): %s", strerror(errno));
        return -1;
    }
    epoll_add_fd(epoll_fd, listen_fd, EPOLLIN);
    return epoll_fd;
}

int epoll_add_fd(int epoll_fd, int fd, uint32_t events) {
    struct epoll_event ev = { .events = events, .data.fd = fd };
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

int epoll_mod_fd(int epoll_fd, int fd, uint32_t events) {
    struct epoll_event ev = { .events = events, .data.fd = fd };
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

int epoll_del_fd(int epoll_fd, int fd) {
    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

static void accept_connections(gateway_ctx_t *gw) {
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(gw->listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            oc_error("accept(): %s", strerror(errno));
            break;
        }

        set_nonblocking(client_fd);

        /* Enable TCP_NODELAY */
        int opt = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        connection_t *conn = connection_create(client_fd);
        if (!conn) {
            close(client_fd);
            continue;
        }

        /* Grow connections array if needed */
        if (client_fd >= gw->max_fd) {
            int new_max = client_fd + 256;
            connection_t **new_arr = realloc(gw->connections,
                                              (size_t)new_max * sizeof(connection_t *));
            if (!new_arr) {
                connection_destroy(conn);
                close(client_fd);
                continue;
            }
            memset(new_arr + gw->max_fd, 0,
                   (size_t)(new_max - gw->max_fd) * sizeof(connection_t *));
            gw->connections = new_arr;
            gw->max_fd = new_max;
        }

        gw->connections[client_fd] = conn;
        gw->active_connections++;
        epoll_add_fd(gw->epoll_fd, client_fd, EPOLLIN | EPOLLET);

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        oc_debug("New connection from %s:%d (fd=%d)",
                 client_ip, ntohs(client_addr.sin_port), client_fd);
    }
}

static void close_connection(gateway_ctx_t *gw, int fd) {
    epoll_del_fd(gw->epoll_fd, fd);
    if (fd < gw->max_fd && gw->connections[fd]) {
        connection_destroy(gw->connections[fd]);
        gw->connections[fd] = NULL;
        gw->active_connections--;
    }
    close(fd);
}

static void handle_client_data(gateway_ctx_t *gw, int fd) {
    if (fd >= gw->max_fd || !gw->connections[fd]) return;

    connection_t *conn = gw->connections[fd];
    int rc = connection_read(conn);
    if (rc < 0) {
        close_connection(gw, fd);
        return;
    }

    /* Try to parse the HTTP request */
    if (conn->state == CONN_READING_HEADERS || conn->state == CONN_READING_BODY) {
        rc = http_parse_request(conn);
        if (rc == 0) {
            /* Request complete, dispatch */
            gw->total_requests++;
            router_dispatch(gw, conn);

            /* After response, check keep-alive */
            if (!conn->keep_alive) {
                close_connection(gw, fd);
            } else {
                /* Reset for next request */
                conn->state = CONN_READING_HEADERS;
                conn->read_len = 0;
                memset(&conn->request, 0, sizeof(conn->request));
            }
        }
        /* rc > 0 means need more data */
    }
}

int epoll_server_run(gateway_ctx_t *gw) {
    struct epoll_event events[256];
    gw->running = true;

    oc_info("Event loop started");

    while (gw->running) {
        int nfds = epoll_wait(gw->epoll_fd, events, 256, 1000); /* 1s timeout for shutdown check */
        if (nfds < 0) {
            if (errno == EINTR) continue;
            oc_error("epoll_wait(): %s", strerror(errno));
            return OC_ERR;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == gw->listen_fd) {
                accept_connections(gw);
            } else if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                close_connection(gw, fd);
            } else if (events[i].events & EPOLLIN) {
                handle_client_data(gw, fd);
            }
        }
    }

    oc_info("Event loop stopped");
    return OC_OK;
}
