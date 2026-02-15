/*
 * PAL Network Implementation for Linux/POSIX
 *
 * Implements socket abstraction for TCP and UDP communication
 * using standard POSIX sockets with non-blocking I/O support.
 */

#define _POSIX_C_SOURCE 200809L

#include "pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

/* ============================================================================
 * Internal Socket Structure
 * ============================================================================ */

struct pal_socket {
    int fd;              /* File descriptor */
    int type;            /* PAL_SOCKET_TCP or PAL_SOCKET_UDP */
    int nonblocking;     /* Non-blocking flag */
};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Convert errno to PAL error code
 */
static int errno_to_pal_error(void)
{
    switch (errno) {
        case ENOMEM:
        case ENOBUFS:
            return PAL_ERR_NOMEM;
        case EINVAL:
        case EAFNOSUPPORT:
        case EPROTONOSUPPORT:
            return PAL_ERR_INVAL;
        case ETIMEDOUT:
            return PAL_ERR_TIMEOUT;
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
            return PAL_ERR_AGAIN;
        case ECONNREFUSED:
        case ECONNRESET:
        case ECONNABORTED:
        case ENETUNREACH:
        case EHOSTUNREACH:
            return PAL_ERR_CONN;
        case EINTR:
            return PAL_ERR_AGAIN;  /* Retry on interrupt */
        default:
            return PAL_ERR_IO;
    }
}

/**
 * Resolve hostname to sockaddr
 * Returns 0 on success, negative PAL error code on failure
 */
static int resolve_address(const char *host, uint16_t port, int socktype,
                          struct sockaddr_storage *addr, socklen_t *addrlen)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    char port_str[16];
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  /* IPv4 for now */
    hints.ai_socktype = socktype;
    
    /* If host is NULL, use INADDR_ANY */
    if (host == NULL) {
        hints.ai_flags = AI_PASSIVE;
    }

    snprintf(port_str, sizeof(port_str), "%u", port);

    ret = getaddrinfo(host, port_str, &hints, &result);
    if (ret != 0) {
        return PAL_ERR_IO;
    }

    if (result == NULL) {
        return PAL_ERR_IO;
    }

    /* Use the first result */
    memcpy(addr, result->ai_addr, result->ai_addrlen);
    *addrlen = result->ai_addrlen;

    freeaddrinfo(result);
    return PAL_OK;
}

/**
 * Extract host and port from sockaddr
 */
static int sockaddr_to_host_port(const struct sockaddr_storage *addr,
                                 char *host, size_t host_len, uint16_t *port)
{
    const struct sockaddr_in *addr4 = (const struct sockaddr_in *)addr;
    
    if (addr->ss_family != AF_INET) {
        return PAL_ERR_INVAL;
    }

    if (host && host_len > 0) {
        if (inet_ntop(AF_INET, &addr4->sin_addr, host, host_len) == NULL) {
            return PAL_ERR_IO;
        }
    }

    if (port) {
        *port = ntohs(addr4->sin_port);
    }

    return PAL_OK;
}

/* ============================================================================
 * Socket Creation and Destruction
 * ============================================================================ */

pal_socket_t *pal_socket_create(int type)
{
    pal_socket_t *sock;
    int fd;
    int socktype;

    if (type != PAL_SOCKET_TCP && type != PAL_SOCKET_UDP) {
        return NULL;
    }

    socktype = (type == PAL_SOCKET_TCP) ? SOCK_STREAM : SOCK_DGRAM;
    
    fd = socket(AF_INET, socktype, 0);
    if (fd < 0) {
        return NULL;
    }

    sock = calloc(1, sizeof(*sock));
    if (!sock) {
        close(fd);
        return NULL;
    }

    sock->fd = fd;
    sock->type = type;
    sock->nonblocking = 0;

    return sock;
}

void pal_socket_close(pal_socket_t *sock)
{
    if (!sock) {
        return;
    }

    if (sock->fd >= 0) {
        close(sock->fd);
    }

    free(sock);
}

/* ============================================================================
 * Connection and Binding
 * ============================================================================ */

int pal_socket_connect(pal_socket_t *sock, const char *host, uint16_t port)
{
    struct sockaddr_storage addr;
    socklen_t addrlen;
    int ret;

    if (!sock || !host) {
        return PAL_ERR_INVAL;
    }

    ret = resolve_address(host, port, 
                         (sock->type == PAL_SOCKET_TCP) ? SOCK_STREAM : SOCK_DGRAM,
                         &addr, &addrlen);
    if (ret < 0) {
        return ret;
    }

    ret = connect(sock->fd, (struct sockaddr *)&addr, addrlen);
    if (ret < 0) {
        if (errno == EINPROGRESS && sock->nonblocking) {
            return PAL_ERR_AGAIN;
        }
        return errno_to_pal_error();
    }

    return PAL_OK;
}

int pal_socket_bind(pal_socket_t *sock, const char *host, uint16_t port)
{
    struct sockaddr_storage addr;
    socklen_t addrlen;
    int ret;
    int reuse = 1;

    if (!sock) {
        return PAL_ERR_INVAL;
    }

    /* Enable address reuse */
    setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    ret = resolve_address(host, port,
                         (sock->type == PAL_SOCKET_TCP) ? SOCK_STREAM : SOCK_DGRAM,
                         &addr, &addrlen);
    if (ret < 0) {
        return ret;
    }

    ret = bind(sock->fd, (struct sockaddr *)&addr, addrlen);
    if (ret < 0) {
        return errno_to_pal_error();
    }

    return PAL_OK;
}

int pal_socket_sendto(pal_socket_t *sock, const void *data, size_t len,
                      const char *host, uint16_t port)
{
    struct sockaddr_storage addr;
    socklen_t addrlen;
    ssize_t ret;
    int res;

    if (!sock || !data || !host) {
        return PAL_ERR_INVAL;
    }

    res = resolve_address(host, port, SOCK_DGRAM, &addr, &addrlen);
    if (res < 0) {
        return res;
    }

    do {
        ret = sendto(sock->fd, data, len, MSG_NOSIGNAL,
                    (struct sockaddr *)&addr, addrlen);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        return errno_to_pal_error();
    }

    return (int)ret;
}

int pal_socket_get_local_addr(pal_socket_t *sock, char *host, size_t host_len,
                              uint16_t *port)
{
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);

    if (!sock) {
        return PAL_ERR_INVAL;
    }

    if (getsockname(sock->fd, (struct sockaddr *)&addr, &addrlen) < 0) {
        return errno_to_pal_error();
    }

    return sockaddr_to_host_port(&addr, host, host_len, port);
}

int pal_socket_get_fd(pal_socket_t *sock)
{
    if (!sock) return -1;
    return sock->fd;
}
