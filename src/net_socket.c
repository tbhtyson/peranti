#define _POSIX_C_SOURCE 200809L

#include "net_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>

void net_socket_open(NetSocket *sock, const char *host, uint16_t port) {
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *res = NULL;
    int gai_err = getaddrinfo(host, port_str, &hints, &res);
    if (gai_err != 0) {
        fprintf(stderr, "net_socket: failed to resolve %s:%u: %s\n",
                host, (unsigned)port, gai_strerror(gai_err));
        exit(1);
    }

    int fd = -1;
    struct addrinfo *it = NULL;
    for (it = res; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd == -1)
            continue;
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd == -1) {
        fprintf(stderr, "net_socket: could not open/connect UDP socket to %s:%u: %s\n",
                host, (unsigned)port, strerror(errno));
        exit(1);
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        fprintf(stderr, "net_socket: failed to set O_NONBLOCK: %s\n", strerror(errno));
        close(fd);
        exit(1);
    }

    sock->fd = fd;
}

int net_socket_send(NetSocket *sock, const void *data, size_t len) {
    ssize_t n = send(sock->fd, data, len, 0);
    if (n < 0) {
        fprintf(stderr, "net_socket: send() failed: %s\n", strerror(errno));
        return -1;
    }
    return (int)n;
}

int net_socket_recv(NetSocket *sock, void *buf, size_t buf_size) {
    ssize_t n = recv(sock->fd, buf, buf_size, 0);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;
        fprintf(stderr, "net_socket: recv() failed: %s\n", strerror(errno));
        return -1;
    }
    return (int)n;
}

void net_socket_close(NetSocket *sock) {
    if (sock->fd >= 0) {
        close(sock->fd);
        sock->fd = -1;
    }
}
