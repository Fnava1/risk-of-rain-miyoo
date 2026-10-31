#ifndef _BS_NET_SOCKET_H_
#define _BS_NET_SOCKET_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_TYPE_TCP 0
#define NET_TYPE_UDP 1

#define NET_EVENT_CONNECT    1
#define NET_EVENT_DISCONNECT 2
#define NET_EVENT_DATA       3

typedef void (*NetEventCallback)(int32_t type, int32_t socketId, const char* ip, int32_t port, const uint8_t* data, size_t size);

int32_t net_socket_create(int32_t type);
int32_t net_connect_async(int32_t socketId, const char* ip, int32_t port);
int32_t net_send_packet(int32_t socketId, const uint8_t* data, size_t size);
int32_t net_server_create(int32_t type, int32_t port, int32_t maxClients);
void    net_socket_destroy(int32_t socketId);
void    net_poll_sockets(NetEventCallback on_event);
int32_t net_socket_get_server_parent(int32_t socketId);

#ifdef __cplusplus
}
#endif

#endif /* _BS_NET_SOCKET_H_ */
