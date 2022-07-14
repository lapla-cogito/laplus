#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "network.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define ETHER_ADDR_LEN 6 /**Ethernetアドレス(MACアドレス)の長さ*/
#define ETHER_ADDR_STR_LEN 18 /* "xx:xx:xx:xx:xx:xx\0" */

#define ETHER_HDR_SIZE 14
#define ETHER_FRAME_SIZE_MIN 60   /* without FCS */
#define ETHER_FRAME_SIZE_MAX 1514 /* without FCS */
#define ETHER_PAYLOAD_SIZE_MIN (ETHER_FRAME_SIZE_MIN - ETHER_HDR_SIZE)
#define ETHER_PAYLOAD_SIZE_MAX (ETHER_FRAME_SIZE_MAX - ETHER_HDR_SIZE)

/* see https://www.iana.org/assignments/ieee-802-numbers/ieee-802-numbers.txt */
#define ETHER_TYPE_IP 0x0800
#define ETHER_TYPE_ARP 0x0806
#define ETHER_TYPE_IPV6 0x86dd

extern const uint8_t ETHER_ADDR_ANY[ETHER_ADDR_LEN];
extern const uint8_t ETHER_ADDR_BROADCAST[ETHER_ADDR_LEN];

extern int ether_addr_pton(const char *p, uint8_t *n);
extern char *ether_addr_ntop(const uint8_t *n, char *p, size_t size);

extern int
ether_transmit_helper(struct net_device *dev, uint16_t type,
                      const uint8_t *payload, size_t plen, const void *dst,
                      ssize_t (*callback)(struct net_device *dev,
                                          const uint8_t *buf, size_t len));
extern int ether_input(const uint8_t *data, size_t len, struct net_device *dev);
extern void ether_setup_helper(struct net_device *net_device);

#ifdef __cplusplus
}
#endif