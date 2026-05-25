/**
 * @file uds_server.c
 * @brief Unix Domain Socket server with epoll multiplexing.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "uds_server.h"
#include "typio/log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <libgen.h>

#define TYPIO_UDS_BACKLOG      8
#define TYPIO_UDS_MAX_CLIENTS  16
#define TYPIO_UDS_READBUF      8192
#define TYPIO_UDS_WRITEBUF     65536
#define TYPIO_UDS_MAX_FRAME    (1U << 20) /* 1 MiB */

typedef struct TypioUdsClient {
    int fd;
    bool closed;

    /* read state */
    uint8_t rbuf[TYPIO_UDS_READBUF];
    size_t  rlen;
    bool    have_len;
    uint32_t frame_len;

    /* write state */
    uint8_t wbuf[TYPIO_UDS_WRITEBUF];
    size_t  wlen;
    size_t  wpos;
} TypioUdsClient;

struct TypioUdsServer {
    char *socket_path;
    int listen_fd;
    int epoll_fd;
    TypioUdsClient clients[TYPIO_UDS_MAX_CLIENTS];
    TypioUdsRequestHandler handler;
    void *handler_user_data;
};

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static bool stale_socket_probe(const char *path)
{
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        close(fd);
        return false; /* someone is listening */
    }
    close(fd);
    return true; /* stale */
}

static int add_epoll(int epoll_fd, int fd, uint32_t events)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = NULL;
    ev.data.fd = fd;
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}


static void del_epoll(int epoll_fd, int fd)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

static TypioUdsClient *find_free_client(TypioUdsServer *srv)
{
    for (int i = 0; i < TYPIO_UDS_MAX_CLIENTS; i++) {
        if (srv->clients[i].fd < 0)
            return &srv->clients[i];
    }
    return NULL;
}

static void close_client(TypioUdsServer *srv, TypioUdsClient *c)
{
    if (!c || c->fd < 0)
        return;
    del_epoll(srv->epoll_fd, c->fd);
    close(c->fd);
    c->fd = -1;
    c->closed = true;
    c->rlen = 0;
    c->have_len = false;
    c->frame_len = 0;
    c->wlen = 0;
    c->wpos = 0;
}

static bool client_write(TypioUdsServer *srv, TypioUdsClient *c)
{
    ssize_t n;
    if (c->wpos >= c->wlen)
        return true;

    n = send(c->fd, c->wbuf + c->wpos, c->wlen - c->wpos, MSG_NOSIGNAL);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        typio_log(TYPIO_LOG_WARNING, "UDS client write error: %s", strerror(errno));
        close_client(srv, c);
        return false;
    }
    c->wpos += (size_t)n;
    if (c->wpos >= c->wlen) {
        c->wpos = 0;
        c->wlen = 0;
    }
    return true;
}

static bool client_enqueue(TypioUdsServer *srv, TypioUdsClient *c,
                            const uint8_t *data, size_t len)
{
    if (c->wlen + len > TYPIO_UDS_WRITEBUF) {
        typio_log(TYPIO_LOG_WARNING, "UDS client write buffer overflow");
        close_client(srv, c);
        return false;
    }
    memcpy(c->wbuf + c->wlen, data, len);
    c->wlen += len;
    return client_write(srv, c);
}

static bool client_send_frame(TypioUdsServer *srv, TypioUdsClient *c,
                               const char *json)
{
    uint32_t len_be;
    size_t json_len = strlen(json);
    if (json_len > TYPIO_UDS_MAX_FRAME) {
        typio_log(TYPIO_LOG_WARNING, "UDS frame too large (%zu)", json_len);
        return false;
    }
    len_be = htonl((uint32_t)json_len);
    if (!client_enqueue(srv, c, (const uint8_t *)&len_be, 4))
        return false;
    if (!client_enqueue(srv, c, (const uint8_t *)json, json_len))
        return false;
    return true;
}

static void client_process_read(TypioUdsServer *srv, TypioUdsClient *c)
{
    ssize_t n;
    size_t consumed;

    n = recv(c->fd, c->rbuf + c->rlen, TYPIO_UDS_READBUF - c->rlen, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        typio_log(TYPIO_LOG_WARNING, "UDS client read error: %s", strerror(errno));
        close_client(srv, c);
        return;
    }
    if (n == 0) {
        close_client(srv, c);
        return;
    }
    c->rlen += (size_t)n;

    /* Try to extract complete frames */
    while (c->rlen > 0) {
        if (!c->have_len) {
            if (c->rlen < 4)
                break;
            c->frame_len = ((uint32_t)c->rbuf[0] << 24) |
                           ((uint32_t)c->rbuf[1] << 16) |
                           ((uint32_t)c->rbuf[2] << 8)  |
                           ((uint32_t)c->rbuf[3]);
            if (c->frame_len > TYPIO_UDS_MAX_FRAME) {
                typio_log(TYPIO_LOG_WARNING, "UDS oversized frame (%u)", c->frame_len);
                close_client(srv, c);
                return;
            }
            c->have_len = true;
        }

        if (c->rlen < 4 + c->frame_len)
            break;

        /* Complete frame at offset 4 */
        {
            char *request = malloc(c->frame_len + 1);
            char *response = NULL;
            if (request) {
                memcpy(request, c->rbuf + 4, c->frame_len);
                request[c->frame_len] = '\0';

                if (srv->handler) {
                    response = srv->handler(request, srv->handler_user_data);
                }
                if (response) {
                    client_send_frame(srv, c, response);
                    free(response);
                }
                free(request);
            }
        }

        consumed = 4 + c->frame_len;
        memmove(c->rbuf, c->rbuf + consumed, c->rlen - consumed);
        c->rlen -= consumed;
        c->have_len = false;
        c->frame_len = 0;
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

TypioUdsServer *typio_uds_server_new(const char *socket_path)
{
    TypioUdsServer *srv;
    struct sockaddr_un addr;
    int fd;

    if (!socket_path)
        return NULL;

    srv = calloc(1, sizeof(*srv));
    if (!srv)
        return NULL;

    srv->socket_path = strdup(socket_path);
    if (!srv->socket_path) {
        free(srv);
        return NULL;
    }

    for (int i = 0; i < TYPIO_UDS_MAX_CLIENTS; i++)
        srv->clients[i].fd = -1;

    /* Ensure parent directory exists */
    {
        char *path_copy = strdup(socket_path);
        if (path_copy) {
            char *dir = dirname(path_copy);
            if (dir && strcmp(dir, ".") != 0 && strcmp(dir, "/") != 0) {
                struct stat st;
                if (stat(dir, &st) != 0) {
                    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
                        typio_log(TYPIO_LOG_WARNING, "UDS failed to create directory %s: %s", dir, strerror(errno));
                    }
                }
            }
            free(path_copy);
        }
    }

    /* Stale socket cleanup */
    if (access(socket_path, F_OK) == 0 && stale_socket_probe(socket_path)) {
        typio_log(TYPIO_LOG_INFO, "Removing stale UDS socket %s", socket_path);
        unlink(socket_path);
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        typio_log(TYPIO_LOG_ERROR, "UDS socket() failed: %s", strerror(errno));
        goto fail;
    }
    srv->listen_fd = fd;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno == EADDRINUSE) {
            typio_log(TYPIO_LOG_INFO, "UDS socket in use, trying to remove stale: %s", socket_path);
            unlink(socket_path);
            if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                typio_log(TYPIO_LOG_ERROR, "UDS bind() failed after unlink: %s", strerror(errno));
                goto fail;
            }
        } else {
            typio_log(TYPIO_LOG_ERROR, "UDS bind() failed: %s", strerror(errno));
            goto fail;
        }
    }

    if (chmod(socket_path, S_IRUSR | S_IWUSR) < 0) {
        typio_log(TYPIO_LOG_WARNING, "UDS chmod() failed: %s", strerror(errno));
    }

    if (listen(fd, TYPIO_UDS_BACKLOG) < 0) {
        typio_log(TYPIO_LOG_ERROR, "UDS listen() failed: %s", strerror(errno));
        goto fail;
    }

    set_nonblocking(fd);

    srv->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (srv->epoll_fd < 0) {
        typio_log(TYPIO_LOG_ERROR, "epoll_create1() failed: %s", strerror(errno));
        goto fail;
    }

    if (add_epoll(srv->epoll_fd, fd, EPOLLIN) < 0) {
        typio_log(TYPIO_LOG_ERROR, "epoll_ctl ADD listen failed: %s", strerror(errno));
        goto fail;
    }

    typio_log(TYPIO_LOG_INFO, "UDS IPC listening on %s", socket_path);
    return srv;

fail:
    typio_uds_server_destroy(srv);
    return NULL;
}

void typio_uds_server_destroy(TypioUdsServer *srv)
{
    if (!srv)
        return;

    for (int i = 0; i < TYPIO_UDS_MAX_CLIENTS; i++) {
        if (srv->clients[i].fd >= 0)
            close_client(srv, &srv->clients[i]);
    }

    if (srv->epoll_fd >= 0)
        close(srv->epoll_fd);
    if (srv->listen_fd >= 0)
        close(srv->listen_fd);
    if (srv->socket_path) {
        unlink(srv->socket_path);
        free(srv->socket_path);
    }
    free(srv);
}

int typio_uds_server_get_fd(TypioUdsServer *srv)
{
    return srv ? srv->epoll_fd : -1;
}

void typio_uds_server_dispatch(TypioUdsServer *srv)
{
    struct epoll_event events[TYPIO_UDS_MAX_CLIENTS + 2];
    int n;

    if (!srv || srv->epoll_fd < 0)
        return;

    n = epoll_wait(srv->epoll_fd, events,
                   TYPIO_UDS_MAX_CLIENTS + 2, 0);
    if (n < 0) {
        if (errno != EINTR)
            typio_log(TYPIO_LOG_WARNING, "UDS epoll_wait error: %s", strerror(errno));
        return;
    }

    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;

        if (fd == srv->listen_fd) {
            /* Accept new connections */
            while (1) {
                int cfd = accept(fd, NULL, NULL);
                TypioUdsClient *c;
                struct ucred cred;
                socklen_t cred_len = sizeof(cred);

                if (cfd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    typio_log(TYPIO_LOG_WARNING, "UDS accept error: %s", strerror(errno));
                    break;
                }

                /* Validate peer credentials */
                if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) == 0) {
                    if (cred.uid != getuid()) {
                        typio_log(TYPIO_LOG_WARNING,
                                  "UDS rejecting connection from uid %u",
                                  (unsigned)cred.uid);
                        close(cfd);
                        continue;
                    }
                }

                c = find_free_client(srv);
                if (!c) {
                    typio_log(TYPIO_LOG_WARNING, "UDS max clients reached");
                    close(cfd);
                    continue;
                }

                c->fd = cfd;
                c->closed = false;
                c->rlen = 0;
                c->have_len = false;
                c->wlen = 0;
                c->wpos = 0;
                set_nonblocking(cfd);

                if (add_epoll(srv->epoll_fd, cfd, EPOLLIN) < 0) {
                    typio_log(TYPIO_LOG_WARNING,
                              "UDS epoll_ctl ADD client failed: %s", strerror(errno));
                    close_client(srv, c);
                }
            }
        } else {
            /* Client I/O */
            TypioUdsClient *c = NULL;
            for (int j = 0; j < TYPIO_UDS_MAX_CLIENTS; j++) {
                if (srv->clients[j].fd == fd) {
                    c = &srv->clients[j];
                    break;
                }
            }
            if (!c)
                continue;

            if (events[i].events & (EPOLLERR | EPOLLHUP))
                close_client(srv, c);
            else if (events[i].events & EPOLLIN)
                client_process_read(srv, c);

            /* Try to drain any pending writes */
            if (c->fd >= 0 && c->wpos < c->wlen)
                client_write(srv, c);
        }
    }
}

void typio_uds_server_set_handler(TypioUdsServer *srv,
                                   TypioUdsRequestHandler handler,
                                   void *user_data)
{
    if (!srv)
        return;
    srv->handler = handler;
    srv->handler_user_data = user_data;
}

void typio_uds_server_broadcast(TypioUdsServer *srv,
                                 const char *json_notification)
{
    if (!srv || !json_notification)
        return;

    for (int i = 0; i < TYPIO_UDS_MAX_CLIENTS; i++) {
        TypioUdsClient *c = &srv->clients[i];
        if (c->fd >= 0 && !c->closed)
            client_send_frame(srv, c, json_notification);
    }
}
