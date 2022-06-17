#include "ethernet.h"
#include "benri.h"
#include "network.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

struct ether_hdr {
    uint8_t dst[ETHER_ADDR_LEN];
    uint8_t src[ETHER_ADDR_LEN];
    uint16_t type;
};

const uint8_t ETHER_ADDR_ANY[ETHER_ADDR_LEN] = {0x00, 0x00, 0x00,
                                                0x00, 0x00, 0x00};
const uint8_t ETHER_ADDR_BROADCAST[ETHER_ADDR_LEN] = {0xff, 0xff, 0xff,
                                                      0xff, 0xff, 0xff};

int ether_addr_pton(const char *p, uint8_t *n) {
    int index;
    char *ep;
    long val;

    if(!p || !n) { return -1; }
    for(index = 0; index < ETHER_ADDR_LEN; index++) {
        val = strtol(p, &ep, 16);
        if(ep == p || val < 0 || val > 0xff ||
           (index < ETHER_ADDR_LEN - 1 && *ep != ':')) {
            break;
        }
        n[index] = (uint8_t)val;
        p = ep + 1;
    }
    if(index != ETHER_ADDR_LEN || *ep != '\0') { return -1; }
    return 0;
}

char *ether_addr_ntop(const uint8_t *n, char *p, size_t size) {
    if(!n || !p) { return NULL; }
    snprintf(p, size, "%02x:%02x:%02x:%02x:%02x:%02x", n[0], n[1], n[2], n[3],
             n[4], n[5]);
    return p;
}

static void ether_dump(const uint8_t *frame, size_t flen) {
    struct ether_hdr *hdr;
    char addr[ETHER_ADDR_STR_LEN];

    hdr = (struct ether_hdr *)frame;
    flockfile(stderr);
    printk("        src: %s\n", ether_addr_ntop(hdr->src, addr, sizeof(addr)));
    printk("        dst: %s\n", ether_addr_ntop(hdr->dst, addr, sizeof(addr)));
    printk("       type: 0x%04x\n", ntoh16(hdr->type));
#ifdef HEXDUMP
    hexdump(stderr, frame, flen);
#endif
    funlockfile(stderr);
}

int ether_transmit_helper(struct net_device *dev, uint16_t type,
                          const uint8_t *data, size_t len, const void *dst,
                          ssize_t (*callback)(struct net_device *dev,
                                              const uint8_t *data,
                                              size_t len)) {
    uint8_t frame[ETHER_FRAME_SIZE_MAX] = {};
    struct ether_hdr *hdr;
    size_t flen, pad = 0;

    hdr = (struct ether_hdr *)frame;
    memcpy(hdr->dst, dst, ETHER_ADDR_LEN);
    memcpy(hdr->src, dev->addr, ETHER_ADDR_LEN);
    hdr->type = hton16(type);
    memcpy(hdr + 1, data, len);
    if(len < ETHER_PAYLOAD_SIZE_MIN) { pad = ETHER_PAYLOAD_SIZE_MIN - len; }
    flen = sizeof(*hdr) + len + pad;
    debugf("dev=%s, type=0x%04x, len=%lu", dev->name, type, flen);
    // ether_dump(frame, flen);
    return callback(dev, frame, flen) == (ssize_t)flen ? 0 : -1;
}

int ether_input(const uint8_t *data, size_t len, struct net_device *dev) {
    struct ether_hdr *hdr;
    uint16_t type;

    if(len < (ssize_t)sizeof(*hdr)) {
        errorf("too short");
        return -1;
    }
    hdr = (struct ether_hdr *)data;
    if(memcmp(dev->addr, hdr->dst, ETHER_ADDR_LEN) != 0) {
        if(memcmp(ETHER_ADDR_BROADCAST, hdr->dst, ETHER_ADDR_LEN) != 0) {
            /* for other host */
            return -1;
        }
    }
    type = ntoh16(hdr->type);
    debugf("dev=%s, type=0x%04x, len=%lu", dev->name, type, len);
    // ether_dump(data, len);
    return net_input_handler(type, (uint8_t *)(hdr + 1), len - sizeof(*hdr),
                             dev);
}

void ether_setup_helper(struct net_device *dev) {
    dev->type = NET_DEVICE_TYPE_ETHERNET;
    dev->mtu = ETHER_PAYLOAD_SIZE_MAX;
    dev->flags = (NET_DEVICE_FLAG_BROADCAST | NET_DEVICE_FLAG_NEED_ARP);
    dev->hlen = ETHER_HDR_SIZE;
    dev->alen = ETHER_ADDR_LEN;
    memcpy(dev->broadcast, ETHER_ADDR_BROADCAST, ETHER_ADDR_LEN);
}