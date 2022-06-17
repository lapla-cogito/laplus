#pragma once
#include "ip.h"
#include "network.h"
#include <stdint.h>

#define ARP_RESOLVE_ERROR -1
#define ARP_RESOLVE_INCOMPLETE 0
#define ARP_RESOLVE_FOUND 1

extern int arp_resolve(struct net_iface *iface, ip_addr_t pa, uint8_t *ha);

extern int arp_init(void);