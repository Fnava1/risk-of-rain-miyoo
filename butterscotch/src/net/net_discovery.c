#include "net/net_discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

void net_discovery_broadcast_beacon(const char* server_name, int32_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;

    int broadcast = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ROR_DISCOVERY_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    char payload[256];
    snprintf(payload, sizeof(payload), "%s%s:%d", ROR_BEACON_MAGIC, server_name ? server_name : "RoR_Host", port);

    sendto(fd, payload, strlen(payload), 0, (struct sockaddr*)&addr, sizeof(addr));
    close(fd);
}

bool net_discovery_find_host(char* out_ip, int32_t* out_port, uint32_t timeout_ms) {
    if (!out_ip || !out_port) return false;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ROR_DISCOVERY_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, (int)timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        char buf[256];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &from_len);
        if (n > 0) {
            buf[n] = '\0';
            if (strncmp(buf, ROR_BEACON_MAGIC, strlen(ROR_BEACON_MAGIC)) == 0) {
                inet_ntop(AF_INET, &from.sin_addr, out_ip, 64);
                char* colon = strrchr(buf, ':');
                if (colon && colon > buf + strlen(ROR_BEACON_MAGIC)) {
                    int p = atoi(colon + 1);
                    *out_port = (p > 0) ? p : ROR_DEFAULT_GAME_PORT;
                } else {
                    *out_port = ROR_DEFAULT_GAME_PORT;
                }
                close(fd);
                return true;
            }
        }
    }

    close(fd);
    return false;
}
