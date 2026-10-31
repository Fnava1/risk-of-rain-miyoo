#include "net/net_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define MAX_NET_SOCKETS 32
#define NET_SEND_BUFFER_SIZE (64 * 1024)
#define NET_RECV_BUFFER_SIZE (64 * 1024)

#define GM_PACKET_MAGIC 0xdeadc0de
#define GM_PACKET_HEADER_SIZE 12

static const char GM_GREETING_STR[18] = "GM:Studio-Connect\0";

/* GameMaker Studio 1.4 Handshake Signatures:
 * Client Stage 2: 0xcafebabe, 0xdeadb00b, 16, 0 (little-endian)
 * Server Stage 2: 0xdeafbead, 0xf00dbeeb, 12 (little-endian)
 */
static const uint8_t GM_STAGE2_CLIENT[16] = {
    0xbe, 0xba, 0xfe, 0xca, 0x0b, 0xb0, 0xad, 0xde,
    0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t GM_STAGE2_REPLY[12] = {
    0xad, 0xbe, 0xaf, 0xde, 0xeb, 0xbe, 0x0d, 0xf0,
    0x0c, 0x00, 0x00, 0x00
};

typedef enum {
    GM_HANDSHAKE_NONE = 0,
    GM_HANDSHAKE_CLIENT_WAIT_GREETING,
    GM_HANDSHAKE_CLIENT_WAIT_REPLY,
    GM_HANDSHAKE_SERVER_WAIT_STAGE2,
    GM_HANDSHAKE_ESTABLISHED
} GMHandshakeState;

typedef enum {
    SOCKET_STATE_FREE = 0,
    SOCKET_STATE_CREATED,
    SOCKET_STATE_CONNECTING,
    SOCKET_STATE_HANDSHAKING,
    SOCKET_STATE_CONNECTED,
    SOCKET_STATE_LISTENING,
    SOCKET_STATE_CLOSED
} SocketState;

typedef struct {
    int fd;
    int32_t type;
    SocketState state;
    GMHandshakeState handshake_state;
    int32_t parent_server_id;
    char remote_ip[64];
    int32_t remote_port;
    uint8_t* send_buf;
    size_t send_len;
    size_t send_capacity;
    uint8_t* recv_stream;
    size_t recv_len;
    size_t recv_capacity;
} NetSocket;

static NetSocket s_sockets[MAX_NET_SOCKETS];
static uint8_t s_recv_buf[NET_RECV_BUFFER_SIZE];
static bool s_initialized = false;

static inline uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void write_u32_le(uint8_t* p, uint32_t val) {
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

static void net_ensure_init(void) {
    if (!s_initialized) {
        memset(s_sockets, 0, sizeof(s_sockets));
        for (int i = 0; i < MAX_NET_SOCKETS; i++) {
            s_sockets[i].fd = -1;
            s_sockets[i].state = SOCKET_STATE_FREE;
            s_sockets[i].handshake_state = GM_HANDSHAKE_NONE;
            s_sockets[i].parent_server_id = -1;
            s_sockets[i].send_buf = NULL;
            s_sockets[i].recv_stream = NULL;
        }
        s_initialized = true;
    }
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_NET_SOCKETS; i++) {
        if (s_sockets[i].state == SOCKET_STATE_FREE) {
            return i;
        }
    }
    return -1;
}

int32_t net_socket_create(int32_t type) {
    net_ensure_init();
    int slot = find_free_slot();
    if (slot < 0) return -1;

    int domain = AF_INET;
    int sock_type = (type == NET_TYPE_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    int fd = socket(domain, sock_type, 0);
    if (fd < 0) return -1;

    if (set_nonblocking(fd) < 0) {
        close(fd);
        return -1;
    }

    uint8_t* sbuf = (uint8_t*)malloc(NET_SEND_BUFFER_SIZE);
    if (!sbuf) {
        close(fd);
        return -1;
    }

    uint8_t* rbuf = (uint8_t*)malloc(NET_RECV_BUFFER_SIZE);
    if (!rbuf) {
        free(sbuf);
        close(fd);
        return -1;
    }

    s_sockets[slot].fd = fd;
    s_sockets[slot].type = type;
    s_sockets[slot].state = (type == NET_TYPE_UDP) ? SOCKET_STATE_CONNECTED : SOCKET_STATE_CREATED;
    s_sockets[slot].handshake_state = GM_HANDSHAKE_NONE;
    s_sockets[slot].parent_server_id = -1;
    s_sockets[slot].remote_ip[0] = '\0';
    s_sockets[slot].remote_port = 0;
    s_sockets[slot].send_buf = sbuf;
    s_sockets[slot].send_len = 0;
    s_sockets[slot].send_capacity = NET_SEND_BUFFER_SIZE;
    s_sockets[slot].recv_stream = rbuf;
    s_sockets[slot].recv_len = 0;
    s_sockets[slot].recv_capacity = NET_RECV_BUFFER_SIZE;

    return (int32_t)slot;
}

int32_t net_connect_async(int32_t socketId, const char* ip, int32_t port) {
    net_ensure_init();
    if (socketId < 0 || socketId >= MAX_NET_SOCKETS) return -1;
    NetSocket* s = &s_sockets[socketId];
    if (s->fd < 0 || s->state == SOCKET_STATE_FREE) return -1;

    strncpy(s->remote_ip, ip, sizeof(s->remote_ip) - 1);
    s->remote_ip[sizeof(s->remote_ip) - 1] = '\0';
    s->remote_port = port;
    s->send_len = 0;
    s->recv_len = 0;
    s->handshake_state = (s->type == NET_TYPE_TCP) ? GM_HANDSHAKE_CLIENT_WAIT_GREETING : GM_HANDSHAKE_NONE;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        return -1;
    }

    int res = connect(s->fd, (struct sockaddr*)&addr, sizeof(addr));
    if (res == 0) {
        s->state = (s->type == NET_TYPE_TCP) ? SOCKET_STATE_HANDSHAKING : SOCKET_STATE_CONNECTED;
        return 0;
    }

    if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
        s->state = SOCKET_STATE_CONNECTING;
        return 0;
    }

    return -1;
}

int32_t net_send_packet(int32_t socketId, const uint8_t* data, size_t size) {
    net_ensure_init();
    if (socketId < 0 || socketId >= MAX_NET_SOCKETS) return -1;
    NetSocket* s = &s_sockets[socketId];
    if (s->fd < 0 || s->state != SOCKET_STATE_CONNECTED) return -1;
    if (!data || size == 0) return 0;

    if (s->type == NET_TYPE_UDP) {
        if (s->remote_port > 0 && s->remote_ip[0] != '\0') {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons((uint16_t)s->remote_port);
            inet_pton(AF_INET, s->remote_ip, &addr.sin_addr);
            ssize_t sent = sendto(s->fd, data, size, 0, (struct sockaddr*)&addr, sizeof(addr));
            return (sent >= 0) ? (int32_t)sent : -1;
        } else {
            ssize_t sent = send(s->fd, data, size, 0);
            return (sent >= 0) ? (int32_t)sent : -1;
        }
    }

    /* TCP send: prepend 12-byte GM:Studio header (0xdeadc0de, 12, size) */
    uint8_t hdr[GM_PACKET_HEADER_SIZE];
    write_u32_le(hdr + 0, GM_PACKET_MAGIC);
    write_u32_le(hdr + 4, GM_PACKET_HEADER_SIZE);
    write_u32_le(hdr + 8, (uint32_t)size);
    size_t total_packet_size = GM_PACKET_HEADER_SIZE + size;

    if (s->send_len == 0) {
        struct iovec iov[2];
        iov[0].iov_base = hdr;
        iov[0].iov_len = GM_PACKET_HEADER_SIZE;
        iov[1].iov_base = (void*)data;
        iov[1].iov_len = size;

        ssize_t sent = writev(s->fd, iov, 2);
        if (sent == (ssize_t)total_packet_size) {
            return (int32_t)size;
        }
        if (sent < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                return -1;
            }
            sent = 0;
        }

        size_t remainder = total_packet_size - (size_t)sent;
        if (s->send_len + remainder <= s->send_capacity) {
            if ((size_t)sent < GM_PACKET_HEADER_SIZE) {
                size_t hdr_rem = GM_PACKET_HEADER_SIZE - (size_t)sent;
                memcpy(s->send_buf + s->send_len, hdr + sent, hdr_rem);
                s->send_len += hdr_rem;
                memcpy(s->send_buf + s->send_len, data, size);
                s->send_len += size;
            } else {
                size_t data_offset = (size_t)sent - GM_PACKET_HEADER_SIZE;
                size_t data_rem = size - data_offset;
                memcpy(s->send_buf + s->send_len, data + data_offset, data_rem);
                s->send_len += data_rem;
            }
            return (int32_t)size;
        }
        return -1; /* Buffer overflow */
    } else {
        if (s->send_len + total_packet_size <= s->send_capacity) {
            memcpy(s->send_buf + s->send_len, hdr, GM_PACKET_HEADER_SIZE);
            s->send_len += GM_PACKET_HEADER_SIZE;
            memcpy(s->send_buf + s->send_len, data, size);
            s->send_len += size;
            return (int32_t)size;
        }
        return -1; /* Buffer overflow */
    }
}

int32_t net_server_create(int32_t type, int32_t port, int32_t maxClients) {
    net_ensure_init();
    int slot = find_free_slot();
    if (slot < 0) return -1;

    int domain = AF_INET;
    int sock_type = (type == NET_TYPE_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    int fd = socket(domain, sock_type, 0);
    if (fd < 0) return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (set_nonblocking(fd) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (type == NET_TYPE_TCP) {
        if (listen(fd, maxClients > 0 ? maxClients : 8) < 0) {
            close(fd);
            return -1;
        }
    }

    s_sockets[slot].fd = fd;
    s_sockets[slot].type = type;
    s_sockets[slot].state = (type == NET_TYPE_TCP) ? SOCKET_STATE_LISTENING : SOCKET_STATE_CONNECTED;
    s_sockets[slot].handshake_state = GM_HANDSHAKE_NONE;
    s_sockets[slot].parent_server_id = -1;
    s_sockets[slot].remote_ip[0] = '\0';
    s_sockets[slot].remote_port = port;
    s_sockets[slot].send_buf = NULL;
    s_sockets[slot].send_len = 0;
    s_sockets[slot].send_capacity = 0;
    s_sockets[slot].recv_stream = NULL;
    s_sockets[slot].recv_len = 0;
    s_sockets[slot].recv_capacity = 0;

    return (int32_t)slot;
}

void net_socket_destroy(int32_t socketId) {
    net_ensure_init();
    if (socketId < 0 || socketId >= MAX_NET_SOCKETS) return;
    NetSocket* s = &s_sockets[socketId];
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
    if (s->send_buf) {
        free(s->send_buf);
        s->send_buf = NULL;
    }
    if (s->recv_stream) {
        free(s->recv_stream);
        s->recv_stream = NULL;
    }
    s->state = SOCKET_STATE_FREE;
    s->handshake_state = GM_HANDSHAKE_NONE;
    s->parent_server_id = -1;
    s->send_len = 0;
    s->send_capacity = 0;
    s->recv_len = 0;
    s->recv_capacity = 0;
    s->remote_ip[0] = '\0';
    s->remote_port = 0;
}

void net_poll_sockets(NetEventCallback on_event) {
    net_ensure_init();
    struct pollfd pfds[MAX_NET_SOCKETS];
    int active_indices[MAX_NET_SOCKETS];
    nfds_t nfds = 0;

    for (int i = 0; i < MAX_NET_SOCKETS; i++) {
        if (s_sockets[i].state != SOCKET_STATE_FREE && s_sockets[i].fd >= 0) {
            pfds[nfds].fd = s_sockets[i].fd;
            pfds[nfds].events = POLLIN;
            if (s_sockets[i].state == SOCKET_STATE_CONNECTING || s_sockets[i].send_len > 0) {
                pfds[nfds].events |= POLLOUT;
            }
            pfds[nfds].revents = 0;
            active_indices[nfds] = i;
            nfds++;
        }
    }

    if (nfds == 0) return;

    int ret = poll(pfds, nfds, 0);
    if (ret <= 0) return;

    for (nfds_t n = 0; n < nfds; n++) {
        int id = active_indices[n];
        NetSocket* s = &s_sockets[id];
        short revents = pfds[n].revents;

        /* Handle error / hup conditions when not readable */
        if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
            if (!(revents & POLLIN)) {
                if (s->state == SOCKET_STATE_CONNECTED || s->state == SOCKET_STATE_CONNECTING || s->state == SOCKET_STATE_HANDSHAKING) {
                    s->state = SOCKET_STATE_CLOSED;
                    if (on_event) {
                        on_event(NET_EVENT_DISCONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                    }
                }
                continue;
            }
        }

        /* Check connect completion */
        if (s->state == SOCKET_STATE_CONNECTING && (revents & POLLOUT)) {
            int err = 0;
            socklen_t elen = sizeof(err);
            if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err == 0) {
                if (s->type == NET_TYPE_TCP) {
                    s->state = SOCKET_STATE_HANDSHAKING;
                    s->handshake_state = GM_HANDSHAKE_CLIENT_WAIT_GREETING;
                } else {
                    s->state = SOCKET_STATE_CONNECTED;
                    if (on_event) {
                        on_event(NET_EVENT_CONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                    }
                }
            } else {
                s->state = SOCKET_STATE_CLOSED;
                if (on_event) {
                    on_event(NET_EVENT_DISCONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                }
                continue;
            }
        }

        /* Flush pending outbound buffer */
        if ((s->state == SOCKET_STATE_CONNECTED || s->state == SOCKET_STATE_HANDSHAKING) && s->send_len > 0 && (revents & POLLOUT)) {
            ssize_t sent = send(s->fd, s->send_buf, s->send_len, MSG_NOSIGNAL);
            if (sent > 0) {
                if ((size_t)sent < s->send_len) {
                    memmove(s->send_buf, s->send_buf + sent, s->send_len - sent);
                    s->send_len -= sent;
                } else {
                    s->send_len = 0;
                }
            }
        }

        /* Handle incoming data during HANDSHAKING */
        if (s->state == SOCKET_STATE_HANDSHAKING && (revents & POLLIN)) {
            if (s->recv_stream && s->recv_len < s->recv_capacity) {
                ssize_t nrecv = recv(s->fd, s->recv_stream + s->recv_len, s->recv_capacity - s->recv_len, 0);
                if (nrecv > 0) {
                    s->recv_len += (size_t)nrecv;
                } else if (nrecv == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    s->state = SOCKET_STATE_CLOSED;
                    if (on_event) on_event(NET_EVENT_DISCONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                    continue;
                }
            }

            // Client handshake: waiting for server greeting
            if (s->handshake_state == GM_HANDSHAKE_CLIENT_WAIT_GREETING) {
                if (s->recv_len >= 3 && memcmp(s->recv_stream, "GM:", 3) != 0) {
                    // Non-GM:S server fallback: treat connection as established
                    s->handshake_state = GM_HANDSHAKE_ESTABLISHED;
                    s->state = SOCKET_STATE_CONNECTED;
                    if (on_event) on_event(NET_EVENT_CONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                } else if (s->recv_len >= 18) {
                    if (memcmp(s->recv_stream, GM_GREETING_STR, 18) == 0) {
                        memmove(s->recv_stream, s->recv_stream + 18, s->recv_len - 18);
                        s->recv_len -= 18;
                        send(s->fd, GM_STAGE2_CLIENT, sizeof(GM_STAGE2_CLIENT), MSG_NOSIGNAL);
                        s->handshake_state = GM_HANDSHAKE_CLIENT_WAIT_REPLY;
                    } else {
                        s->state = SOCKET_STATE_CLOSED;
                        if (on_event) on_event(NET_EVENT_DISCONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                        continue;
                    }
                }
            }

            // Client handshake: waiting for server stage 2 reply
            if (s->handshake_state == GM_HANDSHAKE_CLIENT_WAIT_REPLY) {
                if (s->recv_len >= 12) {
                    if (memcmp(s->recv_stream, GM_STAGE2_REPLY, 12) == 0) {
                        memmove(s->recv_stream, s->recv_stream + 12, s->recv_len - 12);
                        s->recv_len -= 12;
                        s->handshake_state = GM_HANDSHAKE_ESTABLISHED;
                        s->state = SOCKET_STATE_CONNECTED;
                        if (on_event) {
                            on_event(NET_EVENT_CONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                        }
                    } else {
                        s->state = SOCKET_STATE_CLOSED;
                        if (on_event) on_event(NET_EVENT_DISCONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                        continue;
                    }
                }
            }

            // Server handshake: waiting for client stage 2 greeting
            if (s->handshake_state == GM_HANDSHAKE_SERVER_WAIT_STAGE2) {
                if (s->recv_len >= 16) {
                    if (memcmp(s->recv_stream, GM_STAGE2_CLIENT, 16) == 0) {
                        memmove(s->recv_stream, s->recv_stream + 16, s->recv_len - 16);
                        s->recv_len -= 16;
                        send(s->fd, GM_STAGE2_REPLY, sizeof(GM_STAGE2_REPLY), MSG_NOSIGNAL);
                        s->handshake_state = GM_HANDSHAKE_ESTABLISHED;
                        s->state = SOCKET_STATE_CONNECTED;
                        if (on_event) {
                            on_event(NET_EVENT_CONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                        }
                    } else {
                        s->state = SOCKET_STATE_CLOSED;
                        if (on_event) on_event(NET_EVENT_DISCONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                        continue;
                    }
                }
            }
        }

        /* Handle incoming data during CONNECTED */
        if (s->state == SOCKET_STATE_CONNECTED && (revents & POLLIN)) {
            if (s->type == NET_TYPE_TCP) {
                if (s->recv_stream && s->recv_len < s->recv_capacity) {
                    ssize_t nrecv = recv(s->fd, s->recv_stream + s->recv_len, s->recv_capacity - s->recv_len, 0);
                    if (nrecv > 0) {
                        s->recv_len += (size_t)nrecv;
                    } else if (nrecv == 0) {
                        s->state = SOCKET_STATE_CLOSED;
                        if (on_event) {
                            on_event(NET_EVENT_DISCONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                        }
                        continue;
                    } else {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            s->state = SOCKET_STATE_CLOSED;
                            if (on_event) {
                                on_event(NET_EVENT_DISCONNECT, id, s->remote_ip, s->remote_port, NULL, 0);
                            }
                            continue;
                        }
                    }
                }
            } else {
                struct sockaddr_in from;
                socklen_t from_len = sizeof(from);
                ssize_t nrecv = recvfrom(s->fd, s_recv_buf, sizeof(s_recv_buf), 0, (struct sockaddr*)&from, &from_len);
                if (nrecv > 0) {
                    char from_ip[64] = {0};
                    inet_ntop(AF_INET, &from.sin_addr, from_ip, sizeof(from_ip));
                    int32_t from_port = ntohs(from.sin_port);
                    if (on_event) {
                        on_event(NET_EVENT_DATA, id, from_ip, from_port, s_recv_buf, (size_t)nrecv);
                    }
                }
            }
        }

        /* Process packet deframing for TCP connected sockets */
        if (s->state == SOCKET_STATE_CONNECTED && s->type == NET_TYPE_TCP && s->recv_stream && s->recv_len > 0) {
            while (s->recv_len > 0) {
                if (s->recv_len >= 4 && read_u32_le(s->recv_stream) != GM_PACKET_MAGIC) {
                    // Non-framed raw TCP packet fallback
                    if (on_event) {
                        on_event(NET_EVENT_DATA, id, s->remote_ip, s->remote_port, s->recv_stream, s->recv_len);
                    }
                    s->recv_len = 0;
                    break;
                }

                if (s->recv_len < GM_PACKET_HEADER_SIZE) {
                    // Partial header: wait for remainder
                    break;
                }

                uint32_t magic = read_u32_le(s->recv_stream + 0);
                uint32_t hlen  = read_u32_le(s->recv_stream + 4);
                uint32_t plen  = read_u32_le(s->recv_stream + 8);

                if (magic == GM_PACKET_MAGIC && hlen == GM_PACKET_HEADER_SIZE) {
                    if (s->recv_len >= GM_PACKET_HEADER_SIZE + (size_t)plen) {
                        const uint8_t* payload = s->recv_stream + GM_PACKET_HEADER_SIZE;
                        if (on_event) {
                            on_event(NET_EVENT_DATA, id, s->remote_ip, s->remote_port, payload, (size_t)plen);
                        }
                        size_t total_consumed = GM_PACKET_HEADER_SIZE + (size_t)plen;
                        memmove(s->recv_stream, s->recv_stream + total_consumed, s->recv_len - total_consumed);
                        s->recv_len -= total_consumed;
                    } else {
                        // Partial packet: wait for remainder
                        break;
                    }
                } else {
                    // Fallback
                    if (on_event) {
                        on_event(NET_EVENT_DATA, id, s->remote_ip, s->remote_port, s->recv_stream, s->recv_len);
                    }
                    s->recv_len = 0;
                    break;
                }
            }
        }

        /* Handle server incoming connections */
        if (s->state == SOCKET_STATE_LISTENING && (revents & POLLIN)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(s->fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd >= 0) {
                if (set_nonblocking(client_fd) == 0) {
                    int cid = find_free_slot();
                    if (cid >= 0) {
                        uint8_t* c_sbuf = (uint8_t*)malloc(NET_SEND_BUFFER_SIZE);
                        uint8_t* c_rbuf = (uint8_t*)malloc(NET_RECV_BUFFER_SIZE);
                        if (c_sbuf && c_rbuf) {
                            s_sockets[cid].fd = client_fd;
                            s_sockets[cid].type = NET_TYPE_TCP;
                            s_sockets[cid].state = SOCKET_STATE_HANDSHAKING;
                            s_sockets[cid].handshake_state = GM_HANDSHAKE_SERVER_WAIT_STAGE2;
                            s_sockets[cid].parent_server_id = id;
                            inet_ntop(AF_INET, &client_addr.sin_addr, s_sockets[cid].remote_ip, sizeof(s_sockets[cid].remote_ip));
                            s_sockets[cid].remote_port = ntohs(client_addr.sin_port);
                            s_sockets[cid].send_buf = c_sbuf;
                            s_sockets[cid].send_len = 0;
                            s_sockets[cid].send_capacity = NET_SEND_BUFFER_SIZE;
                            s_sockets[cid].recv_stream = c_rbuf;
                            s_sockets[cid].recv_len = 0;
                            s_sockets[cid].recv_capacity = NET_RECV_BUFFER_SIZE;

                            // Send GM:Studio greeting to newly connected client
                            send(client_fd, GM_GREETING_STR, 18, MSG_NOSIGNAL);
                        } else {
                            if (c_sbuf) free(c_sbuf);
                            if (c_rbuf) free(c_rbuf);
                            close(client_fd);
                        }
                    } else {
                        close(client_fd);
                    }
                } else {
                    close(client_fd);
                }
            }
        }
    }
}

int32_t net_socket_get_server_parent(int32_t socketId) {
    if (socketId < 0 || socketId >= MAX_NET_SOCKETS) return -1;
    return s_sockets[socketId].parent_server_id;
}

