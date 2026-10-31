#ifndef _BS_NET_DISCOVERY_H_
#define _BS_NET_DISCOVERY_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROR_DISCOVERY_PORT 11101
#define ROR_DEFAULT_GAME_PORT 11100
#define ROR_BEACON_MAGIC "RoR_BEACON:"

bool net_discovery_find_host(char* out_ip, int32_t* out_port, uint32_t timeout_ms);
void net_discovery_broadcast_beacon(const char* server_name, int32_t port);

#ifdef __cplusplus
}
#endif

#endif /* _BS_NET_DISCOVERY_H_ */
