#ifndef PAL_H
#define PAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_OK           0
#define PAL_ERR         -1
#define PAL_ERR_NOMEM   -2
#define PAL_ERR_INVAL   -3
#define PAL_ERR_TIMEOUT -4
#define PAL_ERR_AGAIN   -5
#define PAL_ERR_CONN    -6
#define PAL_ERR_IO      -7

typedef struct pal_socket pal_socket_t;

#define PAL_SOCKET_TCP  1
#define PAL_SOCKET_UDP  2

pal_socket_t *pal_socket_create(int type);
void pal_socket_close(pal_socket_t *sock);
int pal_socket_bind(pal_socket_t *sock, const char *host, uint16_t port);
int pal_socket_sendto(pal_socket_t *sock, const void *data, size_t len,
                      const char *host, uint16_t port);
int pal_socket_get_local_addr(pal_socket_t *sock, char *host, size_t host_len,
                              uint16_t *port);
int pal_socket_get_fd(pal_socket_t *sock);

#ifdef __cplusplus
}
#endif

#endif /* PAL_H */
