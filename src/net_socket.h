/* --- net_socket.h ---
 * One job: own a single non-blocking UDP socket connected to one peer
 * (the server). Knows nothing about the Luanti wire format above it.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd;
} NetSocket;

/* Resolves host:port, opens a UDP socket, connects it (so send()/recv()
 * work without repeating the address, and so we only ever see datagrams
 * from that one peer), and sets it non-blocking. Hard-fails on any
 * resolution or socket error -- there is no reasonable fallback for
 * "the server address is bad". */
void net_socket_open(NetSocket *sock, const char *host, uint16_t port);

/* Returns bytes sent, or -1 on a real error (EWOULDBLOCK is not expected
 * for UDP sends of this size and is treated as an error). */
int net_socket_send(NetSocket *sock, const void *data, size_t len);

/* Returns bytes received, 0 if nothing is currently available
 * (EWOULDBLOCK/EAGAIN), or -1 on a real error. */
int net_socket_recv(NetSocket *sock, void *buf, size_t buf_size);

void net_socket_close(NetSocket *sock);
