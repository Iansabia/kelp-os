#ifndef EPOLL_SERVER_H
#define EPOLL_SERVER_H

#include "gateway.h"

/* Create a listening socket bound to addr:port */
int epoll_server_listen(const char *addr, int port, int backlog);

/* Create an epoll instance and add the listen fd */
int epoll_server_create(int listen_fd);

/* Run the epoll event loop (blocks until gw->running is false) */
int epoll_server_run(gateway_ctx_t *gw);

/* Add/remove fds from epoll */
int epoll_add_fd(int epoll_fd, int fd, uint32_t events);
int epoll_mod_fd(int epoll_fd, int fd, uint32_t events);
int epoll_del_fd(int epoll_fd, int fd);

#endif /* EPOLL_SERVER_H */
