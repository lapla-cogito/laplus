/**
 * @file udp.c
 *
 * @brief UDPプロトコルの定義が記述されたファイル
 *
 * @note UDP(User Datagram Protocol)はコネクションレス型
 */
#include "udp.h"
#include "benri.h"
#include "ip.h"
#include "network.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define UDP_PCB_SIZE 16

#define UDP_PCB_STATE_FREE 0
#define UDP_PCB_STATE_OPEN 1
#define UDP_PCB_STATE_CLOSING 2

/* see https://tools.ietf.org/html/rfc6335 */
#define UDP_SOURCE_PORT_MIN 49152
#define UDP_SOURCE_PORT_MAX 65535

/**疑似ヘッダ(checksumに利用)*/
struct pseudo_hdr {
    uint32_t src;     /**送信元IPアドレス*/
    uint32_t dst;     /**送信先IPアドレス*/
    uint8_t zero;     /**パディング*/
    uint8_t protocol; /**プロトコル番号*/
    uint16_t len;     /**UDPパケット長*/
};

/**UDPヘッダ*/
struct udp_hdr {
    uint16_t src; /**送信元ポート番号*/
    uint16_t dst; /**送信先ポート番号*/
    uint16_t len; /**パケット長*/
    uint16_t sum; /**checksum*/
};

/**UDPのプロトコルコントロールブロック*/
struct udp_pcb {
    int state; /**ステート情報(両方向のシーケンス番号等)*/
    struct udp_endpoint local;
    struct queue_head queue; /* receive queue */
    cond_t cond;
};

/* NOTE: the data follows immediately after the structure */
struct udp_queue_entry {
    struct udp_endpoint foreign;
    uint16_t len;
};

static mutex_t mutex = MUTEX_INITIALIZER;
static struct udp_pcb pcbs[UDP_PCB_SIZE];

int udp_endpoint_pton(char *p, struct udp_endpoint *n) {
    char *sep;
    char addr[IP_ADDR_STR_LEN] = {};
    long int port;

    sep = strrchr(p, ':');
    if(!sep) { return -1; }
    memcpy(addr, p, sep - p);
    if(ip_addr_pton(addr, &n->addr) == -1) { return -1; }
    port = strtol(sep + 1, NULL, 10);
    if(port <= 0 || port > UINT16_MAX) { return -1; }
    n->port = hton16(port);
    return 0;
}

char *udp_endpoint_ntop(struct udp_endpoint *n, char *p, size_t size) {
    size_t offset;

    ip_addr_ntop(n->addr, p, size);
    offset = strlen(p);
    snprintf(p + offset, size - offset, ":%d", ntoh16(n->port));
    return p;
}

/**UDPのダンプ(パケット表示)*/
static void udp_dump(const uint8_t *data, size_t len) {
    struct udp_hdr *hdr;

    flockfile(stderr);
    hdr = (struct udp_hdr *)data;
    printk("        src: %u\n", ntoh16(hdr->src));
    printk("        dst: %u\n", ntoh16(hdr->dst));
    printk("        len: %u\n", ntoh16(hdr->len));
    printk("        sum: 0x%04x\n", ntoh16(hdr->sum));
#ifdef HEXDUMP
    hexdump(stderr, data, len);
#endif
    funlockfile(stderr);
}

static struct udp_pcb *udp_pcb_alloc(void) {
    struct udp_pcb *pcb;

    for(pcb = pcbs; pcb < tailof(pcbs); pcb++) {
        if(pcb->state == UDP_PCB_STATE_FREE) {
            pcb->state = UDP_PCB_STATE_OPEN;
            cond_init(&pcb->cond, NULL);
            return pcb;
        }
    }
    return NULL;
}

static void udp_pcb_release(struct udp_pcb *pcb) {
    struct queue_entry *entry;

    if(cond_destroy(&pcb->cond) == EBUSY) {
        pcb->state = UDP_PCB_STATE_CLOSING;
        cond_broadcast(&pcb->cond);
        return;
    }
    while((entry = (struct queue_entry *)queue_pop(&pcb->queue)) != NULL) {
        free(entry);
    }
    memset(pcb, 0, sizeof(*pcb));
}

static struct udp_pcb *udp_pcb_select(ip_addr_t addr, uint16_t port) {
    struct udp_pcb *pcb;

    for(pcb = pcbs; pcb < tailof(pcbs); pcb++) {
        if(pcb->state == UDP_PCB_STATE_OPEN) {
            if((pcb->local.addr == IP_ADDR_ANY || pcb->local.addr == addr) &&
               pcb->local.port == port) {
                return pcb;
            }
        }
    }
    return NULL;
}

static struct udp_pcb *udp_pcb_get(int id) {
    struct udp_pcb *pcb;

    if(id < 0 || id >= (int)countof(pcbs)) {
        /* out of range */
        return NULL;
    }
    pcb = &pcbs[id];
    if(pcb->state != UDP_PCB_STATE_OPEN) { return NULL; }
    return pcb;
}

static int udp_pcb_id(struct udp_pcb *pcb) { return indexof(pcbs, pcb); }

static void udp_input(const uint8_t *data, size_t len, ip_addr_t src,
                      ip_addr_t dst, struct ip_iface *iface) {
    struct pseudo_hdr pseudo;
    uint16_t psum = 0;
    struct udp_hdr *hdr;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];
    struct udp_pcb *pcb;
    struct udp_queue_entry *entry;

    if(len < sizeof(*hdr)) {
        errorf("too short");
        return;
    }
    hdr = (struct udp_hdr *)data;
    if(len != ntoh16(hdr->len)) { /* just to make sure */
        errorf("length error: len=%lu, hdr->len=%u", len, ntoh16(hdr->len));
        return;
    }
    pseudo.src = src;
    pseudo.dst = dst;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTOCOL_UDP;
    pseudo.len = hton16(len);
    psum = ~cksum16((uint16_t *)&pseudo, sizeof(pseudo), 0);
    if(cksum16((uint16_t *)hdr, len, psum) != 0) {
        errorf("checksum error: sum=0x%04x, verify=0x%04x", ntoh16(hdr->sum),
               ntoh16(cksum16((uint16_t *)hdr, len, -hdr->sum + psum)));
        return;
    }
    debugf("%s:%d => %s:%d, len=%lu (payload=%lu)",
           ip_addr_ntop(src, addr1, sizeof(addr1)), ntoh16(hdr->src),
           ip_addr_ntop(dst, addr2, sizeof(addr2)), ntoh16(hdr->dst), len,
           len - sizeof(*hdr));
    mutex_lock(&mutex);
    pcb = udp_pcb_select(dst, hdr->dst);
    if(!pcb) {
        /* port is not in use */
        mutex_unlock(&mutex);
        return;
    }
    entry = (struct udp_queue_entry *)calloc(1, sizeof(*entry) +
                                                    (len - sizeof(*hdr)));
    if(!entry) {
        mutex_unlock(&mutex);
        errorf("calloc() failure");
        return;
    }
    entry->foreign.addr = src;
    entry->foreign.port = hdr->src;
    entry->len = len - sizeof(*hdr);
    memcpy(entry + 1, hdr + 1, entry->len);
    if(!queue_push(&pcb->queue, entry)) {
        mutex_unlock(&mutex);
        errorf("queue_push() failure");
        return;
    }
    cond_broadcast(&pcb->cond);
    mutex_unlock(&mutex);
}

ssize_t udp_output(struct udp_endpoint *src, struct udp_endpoint *dst,
                   const uint8_t *data, size_t len) {
    uint8_t buf[IP_PAYLOAD_SIZE_MAX];
    struct udp_hdr *hdr;
    struct pseudo_hdr pseudo;
    uint16_t total, psum = 0;
    char ep1[UDP_ENDPOINT_STR_LEN];
    char ep2[UDP_ENDPOINT_STR_LEN];

    if(len > IP_PAYLOAD_SIZE_MAX - sizeof(*hdr)) {
        errorf("too long");
        return -1;
    }
    hdr = (struct udp_hdr *)buf;
    hdr->src = src->port;
    hdr->dst = dst->port;
    total = sizeof(*hdr) + len;
    hdr->len = hton16(total);
    hdr->sum = 0;
    memcpy(hdr + 1, data, len);
    pseudo.src = src->addr;
    pseudo.dst = dst->addr;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTOCOL_UDP;
    pseudo.len = hton16(total);
    psum = ~cksum16((uint16_t *)&pseudo, sizeof(pseudo), 0);
    hdr->sum = cksum16((uint16_t *)hdr, total, psum);
    debugf("%s => %s, len=%lu (payload=%lu)",
           udp_endpoint_ntop(src, ep1, sizeof(ep1)),
           udp_endpoint_ntop(dst, ep2, sizeof(ep2)), total, len);
    // udp_dump((uint8_t *)hdr, total);
    if(ip_output(IP_PROTOCOL_UDP, (uint8_t *)hdr, total, src->addr,
                 dst->addr) == -1) {
        errorf("ip_output() failure");
        return -1;
    }
    return len;
}

/*
 * UDP User Commands
 */

int udp_open(void) {
    struct udp_pcb *pcb;
    int id;

    mutex_lock(&mutex);
    pcb = udp_pcb_alloc();
    if(!pcb) {
        errorf("udp_pcb_alloc() failure");
        mutex_unlock(&mutex);
        return -1;
    }
    id = udp_pcb_id(pcb);
    mutex_unlock(&mutex);
    return id;
}

int udp_close(int id) {
    struct udp_pcb *pcb;

    mutex_lock(&mutex);
    pcb = udp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found, id=%d", id);
        mutex_unlock(&mutex);
        return -1;
    }
    udp_pcb_release(pcb);
    mutex_unlock(&mutex);
    return 0;
}

int udp_bind(int id, struct udp_endpoint *local) {
    struct udp_pcb *pcb, *exist;
    char ep1[UDP_ENDPOINT_STR_LEN];
    char ep2[UDP_ENDPOINT_STR_LEN];

    mutex_lock(&mutex);
    pcb = udp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found, id=%d", id);
        mutex_unlock(&mutex);
        return -1;
    }
    exist = udp_pcb_select(local->addr, local->port);
    if(exist) {
        errorf("already in use, id=%d, want=%s, exist=%s", id,
               udp_endpoint_ntop(local, ep1, sizeof(ep1)),
               udp_endpoint_ntop(&exist->local, ep2, sizeof(ep2)));
        mutex_unlock(&mutex);
        return -1;
    }
    pcb->local = *local;
    debugf("bound, id=%d, local=%s", id,
           udp_endpoint_ntop(&pcb->local, ep1, sizeof(ep1)));
    mutex_unlock(&mutex);
    return 0;
}

ssize_t udp_sendto(int id, uint8_t *data, size_t len,
                   struct udp_endpoint *foreign) {
    struct udp_pcb *pcb;
    struct udp_endpoint local;
    struct ip_iface *iface;
    char addr[IP_ADDR_STR_LEN];
    uint32_t p;

    mutex_lock(&mutex);
    pcb = udp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found, id=%d", id);
        mutex_unlock(&mutex);
        return -1;
    }
    local.addr = pcb->local.addr;
    if(local.addr == IP_ADDR_ANY) {
        iface = ip_route_get_iface(foreign->addr);
        if(!iface) {
            errorf("iface not found that can reach foreign address, addr=%s",
                   ip_addr_ntop(foreign->addr, addr, sizeof(addr)));
            mutex_unlock(&mutex);
            return -1;
        }
        local.addr = iface->unicast;
        debugf("select local address, addr=%s",
               ip_addr_ntop(local.addr, addr, sizeof(addr)));
    }
    if(!pcb->local.port) {
        for(p = UDP_SOURCE_PORT_MIN; p <= UDP_SOURCE_PORT_MAX; p++) {
            if(!udp_pcb_select(local.addr, hton16(p))) {
                pcb->local.port = hton16(p);
                debugf("dinamic assign local port, port=%d", p);
                break;
            }
        }
        if(!pcb->local.port) {
            debugf("failed to dinamic assign local port, addr=%s",
                   ip_addr_ntop(local.addr, addr, sizeof(addr)));
            mutex_unlock(&mutex);
            return -1;
        }
    }
    local.port = pcb->local.port;
    mutex_unlock(&mutex);
    return udp_output(&local, foreign, data, len);
}

ssize_t udp_recvfrom(int id, uint8_t *buf, size_t size,
                     struct udp_endpoint *foreign) {
    struct udp_pcb *pcb;
    int state;
    struct udp_queue_entry *entry = NULL;
    ssize_t len;

    mutex_lock(&mutex);
    pcb = udp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found, id=%d", id);
        mutex_unlock(&mutex);
        return -1;
    }
    state = pcb->state;
    while(pcb->state == state) {
        entry = (struct udp_queue_entry *)queue_pop(&pcb->queue);
        if(entry) { break; }
        cond_wait(&pcb->cond, &mutex);
    }
    if(!entry) {
        if(pcb->state == UDP_PCB_STATE_CLOSING) {
            udp_pcb_release(pcb);
            mutex_unlock(&mutex);
            return -1;
        }
        mutex_unlock(&mutex);
        errno = EINTR;
        return -1;
    }
    mutex_unlock(&mutex);
    if(foreign) { *foreign = entry->foreign; }
    len = MIN(size, entry->len); /* truncate */
    memcpy(buf, entry + 1, len);
    free(entry);
    return len;
}

int udp_init(void) {
    if(ip_protocol_register(IP_PROTOCOL_UDP, udp_input) == -1) {
        errorf("ip_protocol_register() failure");
        return -1;
    }
    return 0;
}