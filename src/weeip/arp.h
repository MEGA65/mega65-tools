
#ifndef __ARPH__
#define __ARPH__
#include "task.h"
#include "inet.h"
extern bool_t query_cache(IPV4 *ip, EUI48 *mac);
extern void update_cache(IPV4 *ip, EUI48 *mac);
extern void arp_query(IPV4 *ip);
extern void arp_mens();
extern void arp_init();
#endif
