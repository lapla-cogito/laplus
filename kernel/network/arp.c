/**
 * @file arp.c
 *
 * @brief ARP(アドレス解決のためのプロトコル)の実装
 */
#include "arp.h"
#include "benri.h"
#include "ethernet.h"
#include "ip.h"
#include "network.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

/* see https://www.iana.org/assignments/arp-parameters/arp-parameters.txt */
#define ARP_HRD_ETHER 0x0001
/* NOTE: use same value as the Ethernet types */
#define ARP_PRO_IP ETHER_TYPE_IP

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

#define ARP_CACHE_SIZE 32
#define ARP_CACHE_TIMEOUT 30 /* seconds */

#define ARP_CACHE_STATE_FREE 0
#define ARP_CACHE_STATE_INCOMPLETE 1
#define ARP_CACHE_STATE_RESOLVED 2
#define ARP_CACHE_STATE_STATIC 3

/**ARPヘッダ*/
struct arp_hdr {
    uint16_t hrd; /**ハードウェアタイプ*/
    uint16_t pro; /**プロトコルタイプ*/
    uint8_t hln;  /**ハードウェア長(6bytes)*/
    uint8_t pln;  /**プロトコル長(4bytes)*/
    uint16_t op;  /**オペレーション(ARP request->1, ARP reply->2)*/
};

/**ARPパケット本体*/
struct arp_ether {
    struct arp_hdr hdr;
    uint8_t sha[ETHER_ADDR_LEN]; /**送信元のMACアドレス*/
    uint8_t spa[IP_ADDR_LEN];    /**送信元のIPアドレス*/
    uint8_t tha[ETHER_ADDR_LEN]; /**探索先のMACアドレス*/
    uint8_t tpa[IP_ADDR_LEN];    /**探索先のIPアドレス*/
};

/**ARPキャッシュ(テーブル)*/
struct arp_cache {
    unsigned char state;        /**状態表示*/
    ip_addr_t pa;               /**宛先IPアドレス*/
    uint8_t ha[ETHER_ADDR_LEN]; /**宛先MACアドレス(ARP requestにより取得)*/
    struct timeval timestamp; /**取得時刻*/
};

static mutex_t mutex = MUTEX_INITIALIZER;
static struct arp_cache caches[ARP_CACHE_SIZE];

static const char *arp_opcode_ntoa(uint16_t opcode) {
    switch(ntoh16(opcode)) {
    case ARP_OP_REQUEST:
        return "Request";
    case ARP_OP_REPLY:
        return "Reply";
    }
    return "Unknown";
}

static void arp_dump(const uint8_t *data, size_t len) {
    struct arp_ether *message;
    ip_addr_t spa, tpa;
    char addr[128];

    message = (struct arp_ether *)data;
    flockfile(stderr);
    printk("        hrd: 0x%04x\n", ntoh16(message->hdr.hrd));
    printk("        pro: 0x%04x\n", ntoh16(message->hdr.pro));
    printk("        hln: %u\n", message->hdr.hln);
    printk("        pln: %u\n", message->hdr.pln);
    printk("         op: %u (%s)\n", ntoh16(message->hdr.op),
           arp_opcode_ntoa(message->hdr.op));
    printk("        sha: %s\n",
           ether_addr_ntop(message->sha, addr, sizeof(addr)));
    memcpy(&spa, message->spa, sizeof(spa));
    printk("        spa: %s\n", ip_addr_ntop(spa, addr, sizeof(addr)));
    printk("        tha: %s\n",
           ether_addr_ntop(message->tha, addr, sizeof(addr)));
    memcpy(&tpa, message->tpa, sizeof(tpa));
    printk("        tpa: %s\n", ip_addr_ntop(tpa, addr, sizeof(addr)));
#ifdef HEXDUMP
    hexdump(stderr, data, len);
#endif
    funlockfile(stderr);
}

/*
 * ARP Cache
 *
 * NOTE: ARP Cache functions must be called after mutex locked
 */

static struct arp_cache *arp_cache_alloc(void) {
    struct arp_cache *entry, *oldest = NULL;

    for(entry = caches; entry < tailof(caches); entry++) {
        if(entry->state == ARP_CACHE_STATE_FREE) { return entry; }
        if(!oldest || timercmp(&oldest->timestamp, &entry->timestamp, >)) {
            oldest = entry;
        }
    }
    return oldest;
}

static struct arp_cache *arp_cache_select(ip_addr_t pa) {
    struct arp_cache *entry;

    for(entry = caches; entry < tailof(caches); entry++) {
        if(entry->state != ARP_CACHE_STATE_FREE && entry->pa == pa) {
            return entry;
        }
    }
    return NULL;
}

static struct arp_cache *arp_cache_update(ip_addr_t pa, const uint8_t *ha) {
    struct arp_cache *cache;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[ETHER_ADDR_STR_LEN];

    cache = arp_cache_select(pa);
    if(!cache) {
        /* not found */
        return NULL;
    }
    cache->state = ARP_CACHE_STATE_RESOLVED;
    memcpy(cache->ha, ha, ETHER_ADDR_LEN);
    gettimeofday(&cache->timestamp, NULL);
    debugf("UPDATE: pa=%s, ha=%s", ip_addr_ntop(pa, addr1, sizeof(addr1)),
           ether_addr_ntop(ha, addr2, sizeof(addr2)));
    return cache;
}

static struct arp_cache *arp_cache_insert(ip_addr_t pa, const uint8_t *ha) {
    struct arp_cache *cache;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[ETHER_ADDR_STR_LEN];

    cache = arp_cache_alloc();
    if(!cache) {
        errorf("arp_cache_alloc() failure");
        return NULL;
    }
    cache->state = ARP_CACHE_STATE_RESOLVED;
    cache->pa = pa;
    memcpy(cache->ha, ha, ETHER_ADDR_LEN);
    gettimeofday(&cache->timestamp, NULL);
    debugf("INSERT: pa=%s, ha=%s", ip_addr_ntop(pa, addr1, sizeof(addr1)),
           ether_addr_ntop(ha, addr2, sizeof(addr2)));
    return cache;
}

static void arp_cache_delete(struct arp_cache *cache) {
    char addr1[IP_ADDR_STR_LEN];
    char addr2[ETHER_ADDR_STR_LEN];

    debugf("DELETE: pa=%s, ha=%s",
           ip_addr_ntop(cache->pa, addr1, sizeof(addr1)),
           ether_addr_ntop(cache->ha, addr2, sizeof(addr2)));
    cache->state = ARP_CACHE_STATE_FREE;
    cache->pa = 0;
    memset(cache->ha, 0, ETHER_ADDR_LEN);
    timerclear(&cache->timestamp);
}

static int arp_request(struct net_iface *iface, ip_addr_t tpa) {
    struct arp_ether request;

    request.hdr.hrd = hton16(ARP_HRD_ETHER);
    request.hdr.pro = hton16(ARP_PRO_IP);
    request.hdr.hln = ETHER_ADDR_LEN;
    request.hdr.pln = IP_ADDR_LEN;
    request.hdr.op = hton16(ARP_OP_REQUEST);
    memcpy(request.sha, iface->dev->addr, ETHER_ADDR_LEN);
    memcpy(request.spa, &((struct ip_iface *)iface)->unicast, IP_ADDR_LEN);
    memset(request.tha, 0, ETHER_ADDR_LEN);
    memcpy(request.tpa, &tpa, IP_ADDR_LEN);
    debugf("dev=%s, len=%lu", iface->dev->name, sizeof(request));
    // arp_dump((uint8_t *)&request, sizeof(request));
    return net_device_output(iface->dev, ETHER_TYPE_ARP, (uint8_t *)&request,
                             sizeof(request), iface->dev->broadcast);
}

static int arp_reply(struct net_iface *iface, const uint8_t *tha, ip_addr_t tpa,
                     const uint8_t *dst) {
    struct arp_ether reply;

    reply.hdr.hrd = hton16(ARP_HRD_ETHER);
    reply.hdr.pro = hton16(ARP_PRO_IP);
    reply.hdr.hln = ETHER_ADDR_LEN;
    reply.hdr.pln = IP_ADDR_LEN;
    reply.hdr.op = hton16(ARP_OP_REPLY);
    memcpy(reply.sha, iface->dev->addr, ETHER_ADDR_LEN);
    memcpy(reply.spa, &((struct ip_iface *)iface)->unicast, IP_ADDR_LEN);
    memcpy(reply.tha, tha, ETHER_ADDR_LEN);
    memcpy(reply.tpa, &tpa, IP_ADDR_LEN);
    debugf("dev=%s, len=%lu", iface->dev->name, sizeof(reply));
    // arp_dump((uint8_t *)&reply, sizeof(reply));
    return net_device_output(iface->dev, ETHER_TYPE_ARP, (uint8_t *)&reply,
                             sizeof(reply), dst);
}

static void arp_input(const uint8_t *data, size_t len, struct net_device *dev) {
    struct arp_ether *msg;
    ip_addr_t spa, tpa;
    int marge = 0;
    struct net_iface *iface;

    if(len < sizeof(*msg)) {
        errorf("too short");
        return;
    }
    msg = (struct arp_ether *)data;
    if(ntoh16(msg->hdr.hrd) != ARP_HRD_ETHER ||
       msg->hdr.hln != ETHER_ADDR_LEN) {
        errorf("unsupported hardware address");
        return;
    }
    if(ntoh16(msg->hdr.pro) != ARP_PRO_IP || msg->hdr.pln != IP_ADDR_LEN) {
        errorf("unsupported protocol address");
        return;
    }
    debugf("dev=%s, len=%lu", dev->name, len);
    // arp_dump(data, len);
    memcpy(&spa, msg->spa, sizeof(spa));
    memcpy(&tpa, msg->tpa, sizeof(tpa));
    mutex_lock(&mutex);
    if(arp_cache_update(spa, msg->sha)) {
        /* updated */
        marge = 1;
    }
    mutex_unlock(&mutex);
    iface = net_device_get_iface(dev, NET_IFACE_FAMILY_IP);
    if(iface && ((struct ip_iface *)iface)->unicast == tpa) {
        if(!marge) {
            mutex_lock(&mutex);
            arp_cache_insert(spa, msg->sha);
            mutex_unlock(&mutex);
        }
        if(ntoh16(msg->hdr.op) == ARP_OP_REQUEST) {
            arp_reply(iface, msg->sha, spa, msg->sha);
        }
    }
}

int arp_resolve(struct net_iface *iface, ip_addr_t pa, uint8_t *ha) {
    struct arp_cache *cache;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[ETHER_ADDR_STR_LEN];

    if(iface->dev->type != NET_DEVICE_TYPE_ETHERNET) {
        debugf("unsupported hardware address type");
        return ARP_RESOLVE_ERROR;
    }
    if(iface->family != NET_IFACE_FAMILY_IP) {
        debugf("unsupported protocol address type");
        return ARP_RESOLVE_ERROR;
    }
    mutex_lock(&mutex);
    cache = arp_cache_select(pa);
    if(!cache) {
        cache = arp_cache_alloc();
        if(!cache) {
            mutex_unlock(&mutex);
            errorf("arp_cache_alloc() failure");
            return ARP_RESOLVE_ERROR;
        }
        cache->state = ARP_CACHE_STATE_INCOMPLETE;
        cache->pa = pa;
        gettimeofday(&cache->timestamp, NULL);
        arp_request(iface, pa);
        mutex_unlock(&mutex);
        debugf("cache not found, pa=%s",
               ip_addr_ntop(pa, addr1, sizeof(addr1)));
        return ARP_RESOLVE_INCOMPLETE;
    }
    if(cache->state == ARP_CACHE_STATE_INCOMPLETE) {
        arp_request(iface, pa); /* just in case packet loss */
        mutex_unlock(&mutex);
        return ARP_RESOLVE_INCOMPLETE;
    }
    memcpy(ha, cache->ha, ETHER_ADDR_LEN);
    mutex_unlock(&mutex);
    debugf("resolved, pa=%s, ha=%s", ip_addr_ntop(pa, addr1, sizeof(addr1)),
           ether_addr_ntop(ha, addr2, sizeof(addr2)));
    return ARP_RESOLVE_FOUND;
}

static void arp_timer(void) {
    struct arp_cache *entry;
    struct timeval now, diff;

    mutex_lock(&mutex);
    gettimeofday(&now, NULL);
    for(entry = caches; entry < tailof(caches); entry++) {
        if(entry->state != ARP_CACHE_STATE_FREE &&
           entry->state != ARP_CACHE_STATE_STATIC) {
            timersub(&now, &entry->timestamp, &diff);
            if(diff.tv_sec > ARP_CACHE_TIMEOUT) { arp_cache_delete(entry); }
        }
    }
    mutex_unlock(&mutex);
}

int arp_init(void) {
    struct timeval interval = {1, 0};

    if(net_protocol_register(NET_PROTOCOL_TYPE_ARP, arp_input) == -1) {
        errorf("net_protocol_register() failure");
        return -1;
    }
    if(net_timer_register(interval, arp_timer) == -1) {
        errorf("net_timer_register() failure");
        return -1;
    }
    return 0;
}
