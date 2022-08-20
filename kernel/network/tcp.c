/**
 * @file tcp.c
 *
 * @brief TCPプロトコルの定義が記述されたファイル
 *
 * @note TCP(Transmission Control
 * Protocol)はコネクション型の接続なのでデータを送信する際の制御機能が必要
 */
#include "tcp.h"
#include "benri.h"
#include "ip.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**コントロールフラッグ*/
#define TCP_FLG_FIN 0x01
#define TCP_FLG_SYN 0x02
#define TCP_FLG_RST 0x04
#define TCP_FLG_PSH 0x08
#define TCP_FLG_ACK 0x10
#define TCP_FLG_URG 0x20

#define TCP_FLG_IS(x, y) ((x & 0x3f) == (y))
#define TCP_FLG_ISSET(x, y) ((x & 0x3f) & (y) ? 1 : 0)

#define TCP_PCB_SIZE 16

#define TCP_PCB_MODE_RFC793 1
#define TCP_PCB_MODE_SOCKET 2

/**TCPのPCBの状態*/
#define TCP_PCB_STATE_FREE 0
#define TCP_PCB_STATE_CLOSED 1
#define TCP_PCB_STATE_LISTEN 2
#define TCP_PCB_STATE_SYN_SENT 3
#define TCP_PCB_STATE_SYN_RECEIVED 4
#define TCP_PCB_STATE_ESTABLISHED 5
#define TCP_PCB_STATE_FIN_WAIT1 6
#define TCP_PCB_STATE_FIN_WAIT2 7
#define TCP_PCB_STATE_CLOSING 8
#define TCP_PCB_STATE_TIME_WAIT 9
#define TCP_PCB_STATE_CLOSE_WAIT 10
#define TCP_PCB_STATE_LAST_ACK 11

#define TCP_DEFAULT_RTO 200000     /* micro seconds */
#define TCP_RETRANSMIT_DEADLINE 12 /* seconds */
#define TCP_TIMEWAIT_SEC 30        /* substitute for 2MSL */

#define TCP_SOURCE_PORT_MIN 49152
#define TCP_SOURCE_PORT_MAX 65535

/**疑似ヘッダ(checksumに利用)*/
struct pseudo_hdr {
    uint32_t src;     /**送信元IPアドレス*/
    uint32_t dst;     /**送信先IPアドレス*/
    uint8_t zero;     /**パディング*/
    uint8_t protocol; /**プロトコル番号*/
    uint16_t len;     /**UDPパケット長*/
};
/**TCPヘッダ*/
struct tcp_hdr {
    uint16_t src; /**送信元ポート番号*/
    uint16_t dst; /**送信先ポート番号*/
    uint32_t seq; /**シーケンス番号(相手から受信した確認応答番号)*/
    uint32_t ack; /**確認応答番号(相手から受信したseq+データサイズ)*/
    uint8_t off; /**データオフセット*/
    uint8_t
        flg;      /**
                   *コントロールフラグ(URG, ACK, PSH, RST, SYN, FIN)からなる6bit
                   *URG(Urgent):緊急に処理すべきデータが存在
                   *ACK(Acknowledgement):確認応答番号のフィールドが有効(コネクション確立時以外は値が固定で1)
                   *PSH(Push):受信データをバッファリングせずに即座にアプリケーション層に渡す
                   *RST(Reset):コネクション強制切断(異常検知時)
                   *SYN(Synchronize):コネクション確立要求
                   *FIN(Fin):コネクション正常終了
                   */
    uint16_t wnd; /**ウィンドウサイズ(受信側が一度の受信できる最大データ量)*/
    uint16_t sum; /**checksum*/
    uint16_t up; /**緊急ポインタ(緊急データの開始位置を示す)*/
};

struct tcp_segment_info {
    uint32_t seq;
    uint32_t ack;
    uint16_t len;
    uint16_t wnd;
    uint16_t up;
};

/**TCPのプロトコルコントロールブロック*/
struct tcp_pcb {
    int state;
    int mode; /* user command mode */
    struct tcp_endpoint local;
    struct tcp_endpoint foreign;
    struct {
        uint32_t nxt;
        uint32_t una;
        uint16_t wnd;
        uint16_t up;
        uint32_t wl1;
        uint32_t wl2;
    } snd;
    uint32_t iss;
    struct {
        uint32_t nxt;
        uint16_t wnd;
        uint16_t up;
    } rcv;
    uint32_t irs;
    uint16_t mtu;
    uint16_t mss;      /**最大セグメント長(Maximum Segment Size)*/
    uint8_t buf[8192]; /* receive buffer */
    cond_t cond;
    struct queue_head queue; /* retransmit queue */
    struct timeval tw_timer;
    struct tcp_pcb *parent;
    struct queue_head backlog;
};

struct tcp_queue_entry {
    struct timeval first;
    struct timeval last;
    unsigned int rto; /* micro seconds */
    uint32_t seq;
    uint8_t flg;
    size_t len;
};

static mutex_t mutex = MUTEX_INITIALIZER;
static struct tcp_pcb pcbs[TCP_PCB_SIZE];

static ssize_t tcp_output_segment(uint32_t seq, uint32_t ack, uint8_t flg,
                                  uint16_t wnd, uint8_t *data, size_t len,
                                  struct tcp_endpoint *local,
                                  struct tcp_endpoint *foreign);

int tcp_endpoint_pton(char *p, struct tcp_endpoint *n) {
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

char *tcp_endpoint_ntop(struct tcp_endpoint *n, char *p, size_t size) {
    size_t offset;

    ip_addr_ntop(n->addr, p, size);
    offset = strlen(p);
    snprintf(p + offset, size - offset, ":%d", ntoh16(n->port));
    return p;
}

static char *tcp_flg_ntoa(uint8_t flg) {
    static char str[9];

    snprintf(str, sizeof(str), "--%c%c%c%c%c%c",
             TCP_FLG_ISSET(flg, TCP_FLG_URG) ? 'U' : '-',
             TCP_FLG_ISSET(flg, TCP_FLG_ACK) ? 'A' : '-',
             TCP_FLG_ISSET(flg, TCP_FLG_PSH) ? 'P' : '-',
             TCP_FLG_ISSET(flg, TCP_FLG_RST) ? 'R' : '-',
             TCP_FLG_ISSET(flg, TCP_FLG_SYN) ? 'S' : '-',
             TCP_FLG_ISSET(flg, TCP_FLG_FIN) ? 'F' : '-');
    return str;
}

static void tcp_dump(const uint8_t *data, size_t len) {
    struct tcp_hdr *hdr;

    flockfile(stderr);
    hdr = (struct tcp_hdr *)data;
    printk("        src: %u\n", ntoh16(hdr->src));
    printk("        dst: %u\n", ntoh16(hdr->dst));
    printk("        seq: %u\n", ntoh32(hdr->seq));
    printk("        ack: %u\n", ntoh32(hdr->ack));
    printk("        off: 0x%02x (%d)\n", hdr->off, (hdr->off >> 4) << 2);
    printk("        flg: 0x%02x (%s)\n", hdr->flg, tcp_flg_ntoa(hdr->flg));
    printk("        wnd: %u\n", ntoh16(hdr->wnd));
    printk("        sum: 0x%04x\n", ntoh16(hdr->sum));
    printk("         up: %u\n", ntoh16(hdr->up));
#ifdef HEXDUMP
    hexdump(stderr, data, len);
#endif
    funlockfile(stderr);
}

static struct tcp_pcb *tcp_pcb_alloc() {
    struct tcp_pcb *pcb;

    for(pcb = pcbs; pcb < tailof(pcbs); ++pcb) {
        if(pcb->state == TCP_PCB_STATE_FREE) {
            pcb->state = TCP_PCB_STATE_CLOSED;
            cond_init(&pcb->cond, NULL);
            return pcb;
        }
    }
    return NULL;
}

static void tcp_pcb_release(struct tcp_pcb *pcb) {
    struct queue_entry *entry;
    struct tcp_pcb *est;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];

    if(cond_destroy(&pcb->cond) == -1) {
        cond_broadcast(&pcb->cond);
        return;
    }
    while((entry = (struct queue_entry *)queue_pop(&pcb->queue)) != NULL) {
        free(entry);
    }
    while((est = (struct tcp_pcb *)queue_pop(&pcb->backlog)) != NULL) {
        tcp_pcb_release(est);
    }

    debugf("released, local=%s:%d, foreign=%s:%u",
           ip_addr_ntop(pcb->local.addr, addr1, sizeof(addr1)),
           ntoh16(pcb->local.port),
           ip_addr_ntop(pcb->foreign.addr, addr2, sizeof(addr2)),
           ntoh16(pcb->foreign.port));
    memset(pcb, 0, sizeof(*pcb));
}

static struct tcp_pcb *tcp_pcb_select(struct tcp_endpoint *local,
                                      struct tcp_endpoint *foreign) {
    struct tcp_pcb *pcb, *listen_pcb = NULL;

    for(pcb = pcbs; pcb < tailof(pcbs); pcb++) {
        if((pcb->local.addr == IP_ADDR_ANY || pcb->local.addr == local->addr) &&
           pcb->local.port == local->port) {
            if(!foreign) { return pcb; }
            if(pcb->foreign.addr == foreign->addr &&
               pcb->foreign.port == foreign->port) {
                return pcb;
            }
            if(pcb->state == TCP_PCB_STATE_LISTEN) {
                if(pcb->foreign.addr == IP_ADDR_ANY && pcb->foreign.port == 0) {
                    /* LISTENed with wildcard foreign address/port */
                    listen_pcb = pcb;
                }
            }
        }
    }
    return listen_pcb;
}

static struct tcp_pcb *tcp_pcb_get(int id) {
    struct tcp_pcb *pcb;

    if(id < 0 || id >= (int)countof(pcbs)) {
        /* out of range */
        return NULL;
    }
    pcb = &pcbs[id];
    if(pcb->state == TCP_PCB_STATE_FREE) { return NULL; }
    return pcb;
}

static int tcp_pcb_id(struct tcp_pcb *pcb) { return indexof(pcbs, pcb); }

/**TCP再送*/
static int tcp_retransmit_queue_add(struct tcp_pcb *pcb, uint32_t seq,
                                    uint8_t flg, uint8_t *data, size_t len) {
    struct tcp_queue_entry *entry;

    entry = (struct tcp_queue_entry *)calloc(1, sizeof(*entry) + len);
    if(!entry) {
        errorf("calloc() in tcp_retransmit_queue_add() failure");
        return -1;
    }
    entry->rto = TCP_DEFAULT_RTO;
    entry->seq = seq;
    entry->flg = flg;
    entry->len = len;
    memcpy(entry + 1, data, entry->len);
    gettimeofday(&entry->first, NULL);
    entry->last = entry->first;
    if(!queue_push(&pcb->queue, entry)) {
        errorf("queue_push() failure");
        free(entry);
        return -1;
    }
    return 0;
}

static void tcp_retransmit_queue_cleanup(struct tcp_pcb *pcb) {
    struct tcp_queue_entry *entry;

    while((entry = (struct tcp_queue_entry *)queue_peek(&pcb->queue))) {
        if(entry->seq >= pcb->snd.una) { break; }
        entry = (struct tcp_queue_entry *)queue_pop(&pcb->queue);
        debugf("remove, seq=%u, flags=%s, len=%u", entry->seq,
               tcp_flg_ntoa(entry->flg), entry->len);
        free(entry);
    }
    return;
}

static void tcp_retransmit_queue_emit(void *arg, void *data) {
    struct tcp_pcb *pcb;
    struct tcp_queue_entry *entry;
    struct timeval now, diff, timeout;

    pcb = (struct tcp_pcb *)arg;
    entry = (struct tcp_queue_entry *)data;
    gettimeofday(&now, NULL);
    timersub(&now, &entry->first, &diff);
    if(diff.tv_sec >= TCP_RETRANSMIT_DEADLINE) {
        pcb->state = TCP_PCB_STATE_CLOSED;
        cond_broadcast(&pcb->cond);
        return;
    }
    timeout = entry->last;
    timeval_add_usec(&timeout, entry->rto);
    if(timercmp(&now, &timeout, >)) {
        tcp_output_segment(entry->seq, pcb->rcv.nxt, entry->flg, pcb->rcv.wnd,
                           (uint8_t *)(entry + 1), entry->len, &pcb->local,
                           &pcb->foreign);
        entry->last = now;
        entry->rto *= 2;
    }
}

static void tcp_set_timewait_timer(struct tcp_pcb *pcb) {
    gettimeofday(&pcb->tw_timer, NULL);
    pcb->tw_timer.tv_sec += TCP_TIMEWAIT_SEC;
    debugf("start time_wait timer: %d seconds", TCP_TIMEWAIT_SEC);
}

static ssize_t tcp_output_segment(uint32_t seq, uint32_t ack, uint8_t flg,
                                  uint16_t wnd, uint8_t *data, size_t len,
                                  struct tcp_endpoint *local,
                                  struct tcp_endpoint *foreign) {
    uint8_t buf[IP_PAYLOAD_SIZE_MAX] = {};
    struct tcp_hdr *hdr;
    struct pseudo_hdr pseudo;
    uint16_t psum;
    uint16_t total;
    char ep1[TCP_ENDPOINT_STR_LEN];
    char ep2[TCP_ENDPOINT_STR_LEN];

    hdr = (struct tcp_hdr *)buf;
    hdr->src = local->port;
    hdr->dst = foreign->port;
    hdr->seq = hton32(seq);
    hdr->ack = hton32(ack);
    hdr->off = (sizeof(*hdr) >> 2) << 4;
    hdr->flg = flg;
    hdr->wnd = hton16(wnd);
    hdr->sum = 0;
    hdr->up = 0;
    memcpy(hdr + 1, data, len);
    pseudo.src = local->addr;
    pseudo.dst = foreign->addr;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTOCOL_TCP;
    total = sizeof(*hdr) + len;
    pseudo.len = hton16(total);
    psum = ~cksum16((uint16_t *)&pseudo, sizeof(pseudo), 0);
    hdr->sum = cksum16((uint16_t *)hdr, total, psum);
    debugf("%s => %s, len=%lu (payload=%lu)",
           tcp_endpoint_ntop(local, ep1, sizeof(ep1)),
           tcp_endpoint_ntop(foreign, ep2, sizeof(ep2)), total, len);
    // tcp_dump((uint8_t *)hdr, total);
    if(ip_output(IP_PROTOCOL_TCP, (uint8_t *)hdr, total, local->addr,
                 foreign->addr) == -1) {
        return -1;
    }
    return len;
}

static ssize_t tcp_output(struct tcp_pcb *pcb, uint8_t flg, uint8_t *data,
                          size_t len) {
    uint32_t seq;

    seq = pcb->snd.nxt;
    if(TCP_FLG_ISSET(flg, TCP_FLG_SYN)) { seq = pcb->iss; }
    if(TCP_FLG_ISSET(flg, TCP_FLG_SYN | TCP_FLG_FIN) || len) {
        tcp_retransmit_queue_add(pcb, seq, flg, data, len);
    }
    return tcp_output_segment(seq, pcb->rcv.nxt, flg, pcb->rcv.wnd, data, len,
                              &pcb->local, &pcb->foreign);
}

/* rfc793 - section 3.9 [Event Processing > SEGMENT ARRIVES] */
static void tcp_segment_arrives(struct tcp_segment_info *seg, uint8_t flags,
                                uint8_t *data, size_t len,
                                struct tcp_endpoint *local,
                                struct tcp_endpoint *foreign) {
    struct tcp_pcb *pcb, *new_pcb;
    int acceptable = 0;

    pcb = tcp_pcb_select(local, foreign);
    if(!pcb || pcb->state == TCP_PCB_STATE_CLOSED) {
        if(TCP_FLG_ISSET(flags, TCP_FLG_RST)) { return; }
        if(!TCP_FLG_ISSET(flags, TCP_FLG_ACK)) {
            tcp_output_segment(0, seg->seq + seg->len,
                               TCP_FLG_RST | TCP_FLG_ACK, 0, NULL, 0, local,
                               foreign);
        } else {
            tcp_output_segment(seg->ack, 0, TCP_FLG_RST, 0, NULL, 0, local,
                               foreign);
        }
        return;
    }
    switch(pcb->state) {
    case TCP_PCB_STATE_LISTEN:
        /*
         * first check for an RST
         */
        if(TCP_FLG_ISSET(flags, TCP_FLG_RST)) { return; }
        /*
         * second check for an ACK
         */
        if(TCP_FLG_ISSET(flags, TCP_FLG_ACK)) {
            tcp_output_segment(seg->ack, 0, TCP_FLG_RST, 0, NULL, 0, local,
                               foreign);
            return;
        }
        /*
         * third check for an SYN
         */
        if(TCP_FLG_ISSET(flags, TCP_FLG_SYN)) {
            /* ignore: security/compartment check */
            /* ignore: precedence check */
            if(pcb->mode == TCP_PCB_MODE_SOCKET) {
                new_pcb = tcp_pcb_alloc();
                if(!new_pcb) {
                    errorf("tcp_pcb_alloc() failure");
                    return;
                }
                new_pcb->mode = TCP_PCB_MODE_SOCKET;
                new_pcb->parent = pcb;
                pcb = new_pcb;
            }
            pcb->local = *local;
            pcb->foreign = *foreign;
            pcb->rcv.wnd = sizeof(pcb->buf);
            pcb->rcv.nxt = seg->seq + 1;
            pcb->irs = seg->seq;
            pcb->iss = random();
            tcp_output(pcb, TCP_FLG_SYN | TCP_FLG_ACK, NULL, 0);
            pcb->snd.nxt = pcb->iss + 1;
            pcb->snd.una = pcb->iss;
            pcb->state = TCP_PCB_STATE_SYN_RECEIVED;
            /* ignore: Note that any other incoming control or data (combined
               with SYN) will be processed in the SYN-RECEIVED state, but
               processing of SYN and ACK  should not be repeated */
            return;
        }
        /*
         * fourth other text or control
         */
        /* drop segment */
        return;
    case TCP_PCB_STATE_SYN_SENT:
        /*
         * first check the ACK bit
         */
        if(TCP_FLG_ISSET(flags, TCP_FLG_ACK)) {
            if(seg->ack <= pcb->iss || seg->ack > pcb->snd.nxt) {
                tcp_output_segment(seg->ack, 0, TCP_FLG_RST, 0, NULL, 0, local,
                                   foreign);
                return;
            }
            if(pcb->snd.una <= seg->ack && seg->ack <= pcb->snd.nxt) {
                acceptable = 1;
            }
        }
        /*
         * second check the RST bit
         */
        if(TCP_FLG_ISSET(flags, TCP_FLG_RST)) {
            if(acceptable) {
                errorf("connection reset");
                pcb->state = TCP_PCB_STATE_CLOSED;
                tcp_pcb_release(pcb);
            }
            /* drop segment */
            return;
        }
        /*
         * ignore: third check security and precedence
         */
        /*
         * fourth check the SYN bit
         */
        if(TCP_FLG_ISSET(flags, TCP_FLG_SYN)) {
            pcb->rcv.nxt = seg->seq + 1;
            pcb->irs = seg->seq;
            if(acceptable) {
                pcb->snd.una = seg->ack;
                tcp_retransmit_queue_cleanup(pcb);
            }
            if(pcb->snd.una > pcb->iss) {
                pcb->state = TCP_PCB_STATE_ESTABLISHED;
                tcp_output(pcb, TCP_FLG_ACK, NULL, 0);
                /* NOTE: not specified in the RFC793, but send window
                 * initialization required */
                pcb->snd.wnd = seg->wnd;
                pcb->snd.wl1 = seg->seq;
                pcb->snd.wl2 = seg->ack;
                cond_broadcast(&pcb->cond);
                /* ignore: continue processing at the sixth step below where the
                 * URG bit is checked */
                return;
            } else {
                pcb->state = TCP_PCB_STATE_SYN_RECEIVED;
                tcp_output(pcb, TCP_FLG_SYN | TCP_FLG_ACK, NULL, 0);
                /* ignore: If there are other controls or text in the segment,
                 * queue them for processing after the ESTABLISHED state has
                 * been reached */
                return;
            }
        }
        /*
         * fifth, if neither of the SYN or RST bits is set then drop the segment
         * and return
         */
        /* drop segment */
        return;
    }
    /*
     * Otherwise
     */
    /*
     * first check sequence number
     */
    switch(pcb->state) {
    case TCP_PCB_STATE_SYN_RECEIVED:
    case TCP_PCB_STATE_ESTABLISHED:
    case TCP_PCB_STATE_FIN_WAIT1:
    case TCP_PCB_STATE_FIN_WAIT2:
    case TCP_PCB_STATE_CLOSE_WAIT:
    case TCP_PCB_STATE_CLOSING:
    case TCP_PCB_STATE_LAST_ACK:
    case TCP_PCB_STATE_TIME_WAIT:
        if(!seg->len) {
            if(!pcb->rcv.wnd) {
                if(seg->seq == pcb->rcv.nxt) { acceptable = 1; }
            } else {
                if(pcb->rcv.nxt <= seg->seq &&
                   seg->seq < pcb->rcv.nxt + pcb->rcv.wnd) {
                    acceptable = 1;
                }
            }
        } else {
            if(!pcb->rcv.wnd) {
                /* not acceptable */
            } else {
                if((pcb->rcv.nxt <= seg->seq &&
                    seg->seq < pcb->rcv.nxt + pcb->rcv.wnd) ||
                   (pcb->rcv.nxt <= seg->seq + seg->len - 1 &&
                    seg->seq + seg->len - 1 < pcb->rcv.nxt + pcb->rcv.wnd)) {
                    acceptable = 1;
                }
            }
        }
        if(!acceptable) {
            if(!TCP_FLG_ISSET(flags, TCP_FLG_RST)) {
                tcp_output(pcb, TCP_FLG_ACK, NULL, 0);
            }
            return;
        }
        /*
         * In the following it is assumed that the segment is the idealized
         * segment that begins at RCV.NXT and does not exceed the window.
         * One could tailor actual segments to fit this assumption by
         * trimming off any portions that lie outside the window (including
         * SYN and FIN), and only processing further if the segment then
         * begins at RCV.NXT.  Segments with higher begining sequence
         * numbers may be held for later processing.
         */
    }
    /*
     * second check the RST bit
     */
    switch(pcb->state) {
    case TCP_PCB_STATE_SYN_RECEIVED:
        if(TCP_FLG_ISSET(flags, TCP_FLG_RST)) {
            pcb->state = TCP_PCB_STATE_CLOSED;
            tcp_pcb_release(pcb);
            return;
        }
        break;
    case TCP_PCB_STATE_ESTABLISHED:
    case TCP_PCB_STATE_FIN_WAIT1:
    case TCP_PCB_STATE_FIN_WAIT2:
    case TCP_PCB_STATE_CLOSE_WAIT:
        if(TCP_FLG_ISSET(flags, TCP_FLG_RST)) {
            errorf("connection reset");
            pcb->state = TCP_PCB_STATE_CLOSED;
            tcp_pcb_release(pcb);
            return;
        }
        break;
    case TCP_PCB_STATE_CLOSING:
    case TCP_PCB_STATE_LAST_ACK:
    case TCP_PCB_STATE_TIME_WAIT:
        if(TCP_FLG_ISSET(flags, TCP_FLG_RST)) {
            pcb->state = TCP_PCB_STATE_CLOSED;
            tcp_pcb_release(pcb);
            return;
        }
        break;
    }
    /*
     * ignore: third check security and precedence
     */
    /*
     * fourth check the SYN bit
     */
    switch(pcb->state) {
    case TCP_PCB_STATE_SYN_RECEIVED:
    case TCP_PCB_STATE_ESTABLISHED:
    case TCP_PCB_STATE_FIN_WAIT1:
    case TCP_PCB_STATE_FIN_WAIT2:
    case TCP_PCB_STATE_CLOSE_WAIT:
    case TCP_PCB_STATE_CLOSING:
    case TCP_PCB_STATE_LAST_ACK:
    case TCP_PCB_STATE_TIME_WAIT:
        if(TCP_FLG_ISSET(flags, TCP_FLG_SYN)) {
            tcp_output(pcb, TCP_FLG_RST, NULL, 0);
            errorf("connection reset");
            pcb->state = TCP_PCB_STATE_CLOSED;
            tcp_pcb_release(pcb);
            return;
        }
    }
    /*
     * fifth check the ACK field
     */
    if(!TCP_FLG_ISSET(flags, TCP_FLG_ACK)) {
        /* drop segment */
        return;
    }
    switch(pcb->state) {
    case TCP_PCB_STATE_SYN_RECEIVED:
        if(pcb->snd.una <= seg->ack && seg->ack <= pcb->snd.nxt) {
            pcb->state = TCP_PCB_STATE_ESTABLISHED;
            cond_broadcast(&pcb->cond);
            if(pcb->parent) {
                queue_push(&pcb->parent->backlog, pcb);
                cond_broadcast(&pcb->parent->cond);
            }
        } else {
            tcp_output_segment(seg->ack, 0, TCP_FLG_RST, 0, NULL, 0, local,
                               foreign);
            return;
        }
        /* fall through */
    case TCP_PCB_STATE_ESTABLISHED:
    case TCP_PCB_STATE_FIN_WAIT1:
    case TCP_PCB_STATE_FIN_WAIT2:
    case TCP_PCB_STATE_CLOSE_WAIT:
    case TCP_PCB_STATE_CLOSING:
        if(pcb->snd.una < seg->ack && seg->ack <= pcb->snd.nxt) {
            pcb->snd.una = seg->ack;
            tcp_retransmit_queue_cleanup(pcb);
            /* ignore: Users should receive positive acknowledgments for buffers
                        which have been SENT and fully acknowledged (i.e., SEND
               buffer should be returned with "ok" response) */
            if(pcb->snd.wl1 < seg->seq ||
               (pcb->snd.wl1 == seg->seq && pcb->snd.wl2 <= seg->ack)) {
                pcb->snd.wnd = seg->wnd;
                pcb->snd.wl1 = seg->seq;
                pcb->snd.wl2 = seg->ack;
            }
        } else if(seg->ack < pcb->snd.una) {
            /* ignore */
        } else if(seg->ack > pcb->snd.nxt) {
            tcp_output(pcb, TCP_FLG_ACK, NULL, 0);
            return;
        }
        switch(pcb->state) {
        case TCP_PCB_STATE_FIN_WAIT1:
            if(seg->ack == pcb->snd.nxt) {
                pcb->state = TCP_PCB_STATE_FIN_WAIT2;
            }
            break;
        case TCP_PCB_STATE_FIN_WAIT2:
            /* do not delete the TCB */
            break;
        case TCP_PCB_STATE_CLOSE_WAIT:
            /* do nothing */
            break;
        case TCP_PCB_STATE_CLOSING:
            if(seg->ack == pcb->snd.nxt) {
                pcb->state = TCP_PCB_STATE_TIME_WAIT;
                /* NOTE: set 2MSL timer, although it is not explicitly stated in
                 * the RFC */
                tcp_set_timewait_timer(pcb);
                cond_broadcast(&pcb->cond);
            }
            break;
        }
        break;
    case TCP_PCB_STATE_LAST_ACK:
        if(seg->ack == pcb->snd.nxt) {
            pcb->state = TCP_PCB_STATE_CLOSED;
            tcp_pcb_release(pcb);
        }
        return;
    case TCP_PCB_STATE_TIME_WAIT:
        if(TCP_FLG_ISSET(flags, TCP_FLG_FIN)) {
            tcp_set_timewait_timer(pcb); /* restart time-wait timer */
        }
        break;
    }
    /*
     * ignore: sixth, check the URG bit
     */
    /*
     * seventh, process the segment text
     */
    switch(pcb->state) {
    case TCP_PCB_STATE_ESTABLISHED:
    case TCP_PCB_STATE_FIN_WAIT1:
    case TCP_PCB_STATE_FIN_WAIT2:
        if(len) {
            memcpy(pcb->buf + (sizeof(pcb->buf) - pcb->rcv.wnd), data, len);
            pcb->rcv.nxt = seg->seq + seg->len;
            pcb->rcv.wnd -= len;
            tcp_output(pcb, TCP_FLG_ACK, NULL, 0);
            cond_broadcast(&pcb->cond);
        }
        break;
    case TCP_PCB_STATE_CLOSE_WAIT:
    case TCP_PCB_STATE_CLOSING:
    case TCP_PCB_STATE_LAST_ACK:
    case TCP_PCB_STATE_TIME_WAIT:
        /* ignore segment text */
        break;
    }

    /*
     * eighth, check the FIN bit
     */
    if(TCP_FLG_ISSET(flags, TCP_FLG_FIN)) {
        switch(pcb->state) {
        case TCP_PCB_STATE_CLOSED:
        case TCP_PCB_STATE_LISTEN:
        case TCP_PCB_STATE_SYN_SENT:
            /* drop segment */
            return;
        }
        pcb->rcv.nxt = seg->seq + 1;
        tcp_output(pcb, TCP_FLG_ACK, NULL, 0);
        switch(pcb->state) {
        case TCP_PCB_STATE_SYN_RECEIVED:
        case TCP_PCB_STATE_ESTABLISHED:
            pcb->state = TCP_PCB_STATE_CLOSE_WAIT;
            cond_broadcast(&pcb->cond);
            break;
        case TCP_PCB_STATE_FIN_WAIT1:
            if(seg->ack == pcb->snd.nxt) {
                pcb->state = TCP_PCB_STATE_TIME_WAIT;
                tcp_set_timewait_timer(pcb);
            } else {
                pcb->state = TCP_PCB_STATE_CLOSING;
            }
            break;
        case TCP_PCB_STATE_FIN_WAIT2:
            pcb->state = TCP_PCB_STATE_TIME_WAIT;
            tcp_set_timewait_timer(pcb);
            break;
        case TCP_PCB_STATE_CLOSE_WAIT:
            /* Remain in the CLOSE-WAIT state */
            break;
        case TCP_PCB_STATE_CLOSING:
            /* Remain in the CLOSING state */
            break;
        case TCP_PCB_STATE_LAST_ACK:
            /* Remain in the LAST-ACK state */
            break;
        case TCP_PCB_STATE_TIME_WAIT:
            /* Remain in the TIME-WAIT state */
            tcp_set_timewait_timer(pcb); /* restart time-wait timer */
            break;
        }
    }
    return;
}

static void tcp_input(const uint8_t *data, size_t len, ip_addr_t src,
                      ip_addr_t dst, struct ip_iface *iface) {
    struct tcp_hdr *hdr;
    struct pseudo_hdr pseudo;
    uint16_t psum, hlen;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];
    struct tcp_endpoint local, foreign;
    struct tcp_segment_info seg;

    if(len < sizeof(*hdr)) {
        errorf("too short");
        return;
    }
    hdr = (struct tcp_hdr *)data;
    pseudo.src = src;
    pseudo.dst = dst;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTOCOL_TCP;
    pseudo.len = hton16(len);
    psum = ~cksum16((uint16_t *)&pseudo, sizeof(pseudo), 0);
    if(cksum16((uint16_t *)hdr, len, psum) != 0) {
        errorf("checksum error: sum=0x%04x, verify=0x%04x", ntoh16(hdr->sum),
               ntoh16(cksum16((uint16_t *)hdr, len, -hdr->sum + psum)));
        return;
    }
    if(src == IP_ADDR_BROADCAST || src == iface->broadcast ||
       dst == IP_ADDR_BROADCAST || dst == iface->broadcast) {
        errorf("only supports unicast, src=%s, dst=%s",
               ip_addr_ntop(src, addr1, sizeof(addr1)),
               ip_addr_ntop(dst, addr2, sizeof(addr2)));
        return;
    }
    debugf("%s:%d => %s:%d, len=%lu (payload=%lu)",
           ip_addr_ntop(src, addr1, sizeof(addr1)), ntoh16(hdr->src),
           ip_addr_ntop(dst, addr2, sizeof(addr2)), ntoh16(hdr->dst), len,
           len - sizeof(*hdr));

    local.addr = dst;
    local.port = hdr->dst;
    foreign.addr = src;
    foreign.port = hdr->src;
    hlen = (hdr->off >> 4) << 2;
    seg.seq = ntoh32(hdr->seq);
    seg.ack = ntoh32(hdr->ack);
    seg.len = len - hlen;
    if(TCP_FLG_ISSET(hdr->flg, TCP_FLG_SYN)) {
        seg.len++; /* SYN flag consumes one sequence number */
    }
    if(TCP_FLG_ISSET(hdr->flg, TCP_FLG_FIN)) {
        seg.len++; /* FIN flag consumes one sequence number */
    }
    seg.wnd = ntoh16(hdr->wnd);
    seg.up = ntoh16(hdr->up);
    mutex_lock(&mutex);
    tcp_segment_arrives(&seg, hdr->flg, (uint8_t *)hdr + hlen, len - hlen,
                        &local, &foreign);
    mutex_unlock(&mutex);
    return;
}

static void tcp_timer(void) {
    struct tcp_pcb *pcb;
    struct timeval now;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];

    mutex_lock(&mutex);
    gettimeofday(&now, NULL);
    for(pcb = pcbs; pcb < tailof(pcbs); pcb++) {
        if(pcb->state == TCP_PCB_STATE_FREE) { continue; }
        if(pcb->state == TCP_PCB_STATE_TIME_WAIT) {
            if(timercmp(&now, &pcb->tw_timer, >) != 0) {
                debugf("timewait has elapsed, local=%s:%u, foreign=%s:%u",
                       ip_addr_ntop(pcb->local.addr, addr1, sizeof(addr1)),
                       ntoh16(pcb->local.port),
                       ip_addr_ntop(pcb->foreign.addr, addr2, sizeof(addr2)),
                       ntoh16(pcb->foreign.port));
                tcp_pcb_release(pcb);
                continue;
            }
        }
        queue_foreach(&pcb->queue, tcp_retransmit_queue_emit, pcb);
    }
    mutex_unlock(&mutex);
}

int tcp_init() {
    struct timeval interval = {0, 100000};

    if(ip_protocol_register(IP_PROTOCOL_TCP, tcp_input) == -1) {
        errorf("ip_protocol_register() failure");
        return -1;
    }
    if(net_timer_register(interval, tcp_timer) == -1) {
        errorf("net_timer_register() failure");
        return -1;
    }
    return 0;
}

/*
 * TCP User Command (RFC793)
 */

int tcp_open_rfc793(struct tcp_endpoint *local, struct tcp_endpoint *foreign,
                    int active) {
    struct tcp_pcb *pcb;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];
    int state, id;

    mutex_lock(&mutex);
    pcb = tcp_pcb_alloc();
    if(!pcb) {
        errorf("tcp_pcb_alloc() failure");
        mutex_unlock(&mutex);
        return -1;
    }
    pcb->mode = TCP_PCB_MODE_RFC793;
    if(!active) {
        debugf("passive open: local=%s:%u, waiting for connection...",
               ip_addr_ntop(local->addr, addr1, sizeof(addr1)),
               ntoh16(local->port));
        pcb->local = *local;
        if(foreign) { pcb->foreign = *foreign; }
        pcb->state = TCP_PCB_STATE_LISTEN;
    } else {
        debugf("active open: local=%s:%u, foreign=%s:%u, connecting...",
               ip_addr_ntop(local->addr, addr1, sizeof(addr1)),
               ntoh16(local->port),
               ip_addr_ntop(foreign->addr, addr2, sizeof(addr2)),
               ntoh16(foreign->port));
        pcb->local = *local;
        pcb->foreign = *foreign;
        pcb->rcv.wnd = sizeof(pcb->buf);
        pcb->iss = random();
        if(tcp_output(pcb, TCP_FLG_SYN, NULL, 0) == -1) {
            errorf("tcp_output() failure");
            pcb->state = TCP_PCB_STATE_CLOSED;
            tcp_pcb_release(pcb);
            mutex_unlock(&mutex);
            return -1;
        }
        pcb->snd.una = pcb->iss;
        pcb->snd.nxt = pcb->iss + 1;
        pcb->state = TCP_PCB_STATE_SYN_SENT;
    }
AGAIN:
    state = pcb->state;
    /* waiting for state changed */
    while(pcb->state == state) { cond_wait(&pcb->cond, &mutex); }
    if(pcb->state != TCP_PCB_STATE_ESTABLISHED) {
        if(pcb->state == TCP_PCB_STATE_SYN_RECEIVED) { goto AGAIN; }
        errorf("open error: %d", pcb->state);
        pcb->state = TCP_PCB_STATE_CLOSED;
        tcp_pcb_release(pcb);
        mutex_unlock(&mutex);
        return -1;
    }
    id = tcp_pcb_id(pcb);
    debugf("connection established: local=%s:%u, foreign=%s:%u",
           ip_addr_ntop(pcb->local.addr, addr1, sizeof(addr1)),
           ntoh16(pcb->local.port),
           ip_addr_ntop(pcb->foreign.addr, addr2, sizeof(addr2)),
           ntoh16(pcb->foreign.port));
    mutex_unlock(&mutex);
    return id;
}

int tcp_state(int id) {
    struct tcp_pcb *pcb;
    int state;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
    if(pcb->mode != TCP_PCB_MODE_RFC793) {
        errorf("not opened in rfc793 mode");
        mutex_unlock(&mutex);
        return -1;
    }
    state = pcb->state;
    mutex_unlock(&mutex);
    return state;
}

/*
 * TCP User Command (Socket)
 */

int tcp_open() {
    struct tcp_pcb *pcb;
    int id;

    mutex_lock(&mutex);
    pcb = tcp_pcb_alloc();
    if(!pcb) {
        errorf("tcp_pcb_alloc() failure");
        mutex_unlock(&mutex);
        return -1;
    }
    pcb->mode = TCP_PCB_MODE_SOCKET;
    id = tcp_pcb_id(pcb);
    mutex_unlock(&mutex);
    return id;
}

int tcp_connect(int id, struct tcp_endpoint *foreign) {
    struct tcp_pcb *pcb;
    struct tcp_endpoint local;
    struct ip_iface *iface;
    char addr[IP_ADDR_STR_LEN];
    int p;
    int state;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
    if(pcb->mode != TCP_PCB_MODE_SOCKET) {
        errorf("not opened in socket mode");
        mutex_unlock(&mutex);
        return -1;
    }
    local.addr = pcb->local.addr;
    local.port = pcb->local.port;
    if(local.addr == IP_ADDR_ANY) {
        iface = ip_route_get_iface(foreign->addr);
        if(!iface) {
            errorf("ip_route_get_iface() failure");
            mutex_unlock(&mutex);
            return -1;
        }
        debugf("select source address: %s",
               ip_addr_ntop(iface->unicast, addr, sizeof(addr)));
        local.addr = iface->unicast;
    }
    if(!local.port) {
        for(p = TCP_SOURCE_PORT_MIN; p <= TCP_SOURCE_PORT_MAX; p++) {
            local.port = p;
            if(!tcp_pcb_select(&local, foreign)) {
                debugf("dinamic assign srouce port: %d", ntoh16(local.port));
                pcb->local.port = local.port;
                break;
            }
        }
        if(!local.port) {
            debugf("failed to dinamic assign srouce port");
            mutex_unlock(&mutex);
            return -1;
        }
    }
    pcb->local.addr = local.addr;
    pcb->local.port = local.port;
    pcb->foreign.addr = foreign->addr;
    pcb->foreign.port = foreign->port;
    pcb->rcv.wnd = sizeof(pcb->buf);
    pcb->iss = random();
    if(tcp_output(pcb, TCP_FLG_SYN, NULL, 0) == -1) {
        errorf("tcp_output() failure");
        pcb->state = TCP_PCB_STATE_CLOSED;
        tcp_pcb_release(pcb);
        mutex_unlock(&mutex);
        return -1;
    }
    pcb->snd.una = pcb->iss;
    pcb->snd.nxt = pcb->iss + 1;
    pcb->state = TCP_PCB_STATE_SYN_SENT;
AGAIN:
    state = pcb->state;
    // waiting for state changed
    while(pcb->state == state) { cond_wait(&pcb->cond, &mutex); }
    if(pcb->state != TCP_PCB_STATE_ESTABLISHED) {
        if(pcb->state == TCP_PCB_STATE_SYN_RECEIVED) { goto AGAIN; }
        errorf("open error: %d", pcb->state);
        pcb->state = TCP_PCB_STATE_CLOSED;
        tcp_pcb_release(pcb);
        mutex_unlock(&mutex);
        return -1;
    }
    id = tcp_pcb_id(pcb);
    mutex_unlock(&mutex);
    return id;
}

int tcp_bind(int id, struct tcp_endpoint *local) {
    struct tcp_pcb *pcb, *exist;
    char addr[IP_ADDR_STR_LEN];

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
    if(pcb->mode != TCP_PCB_MODE_SOCKET) {
        errorf("not opened in socket mode");
        mutex_unlock(&mutex);
        return -1;
    }
    exist = tcp_pcb_select(local, NULL);
    if(exist) {
        errorf("already bound, addr=%s, port=%u",
               ip_addr_ntop(exist->local.addr, addr, sizeof(addr)),
               ntoh16(exist->local.port));
        mutex_unlock(&mutex);
        return -1;
    }
    pcb->local = *local;
    debugf("success: addr=%s, port=%u",
           ip_addr_ntop(pcb->local.addr, addr, sizeof(addr)),
           ntoh16(pcb->local.port));
    mutex_unlock(&mutex);
    return 0;
}

int tcp_listen(int id, int backlog) {
    struct tcp_pcb *pcb;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
    if(pcb->mode != TCP_PCB_MODE_SOCKET) {
        errorf("not opened in socket mode");
        mutex_unlock(&mutex);
        return -1;
    }
    pcb->state = TCP_PCB_STATE_LISTEN;
    (void)backlog; // TODO: set backlog
    mutex_unlock(&mutex);
    return 0;
}

int tcp_accept(int id, struct tcp_endpoint *foreign) {
    struct tcp_pcb *pcb, *new_pcb;
    int new_id;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
    if(pcb->mode != TCP_PCB_MODE_SOCKET) {
        errorf("not opened in socket mode");
        mutex_unlock(&mutex);
        return -1;
    }
    if(pcb->state != TCP_PCB_STATE_LISTEN) {
        errorf("not in LISTEN state");
        mutex_unlock(&mutex);
        return -1;
    }
    while(!(new_pcb = (struct tcp_pcb *)queue_pop(&pcb->backlog))) {
        cond_wait(&pcb->cond, &mutex);
    }
    if(foreign) { *foreign = new_pcb->foreign; }
    new_id = tcp_pcb_id(new_pcb);
    mutex_unlock(&mutex);
    return new_id;
}

/*
 * TCP User Command (Common)
 */

ssize_t tcp_send(int id, uint8_t *data, size_t len) {
    struct tcp_pcb *pcb;
    ssize_t sent = 0;
    struct ip_iface *iface;
    size_t mss, cap, slen;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
RETRY:
    switch(pcb->state) {
    case TCP_PCB_STATE_CLOSED:
        errorf("connection does not exist");
        mutex_unlock(&mutex);
        return -1;
    case TCP_PCB_STATE_LISTEN:
        // ignore: change the connection from passive to active
        errorf("this connection is passive");
        mutex_unlock(&mutex);
        return -1;
    case TCP_PCB_STATE_SYN_SENT:
    case TCP_PCB_STATE_SYN_RECEIVED:
        // ignore: Queue the data for transmission after entering ESTABLISHED
        // state
        errorf("insufficient resources");
        mutex_unlock(&mutex);
        return -1;
    case TCP_PCB_STATE_ESTABLISHED:
    case TCP_PCB_STATE_CLOSE_WAIT:
        iface = ip_route_get_iface(pcb->local.addr);
        if(!iface) {
            errorf("iface not found");
            mutex_unlock(&mutex);
            return -1;
        }
        mss = NET_IFACE(iface)->dev->mtu -
              (IP_HDR_SIZE_MIN + sizeof(struct tcp_hdr));
        while(sent < (ssize_t)len) {
            cap = pcb->snd.wnd - (pcb->snd.nxt - pcb->snd.una);
            if(!cap) {
                cond_wait(&pcb->cond, &mutex);
                goto RETRY;
            }
            slen = MIN(MIN(mss, len - sent), cap);
            if(tcp_output(pcb, TCP_FLG_ACK | TCP_FLG_PSH, data + sent, slen) ==
               -1) {
                errorf("tcp_output() failure");
                pcb->state = TCP_PCB_STATE_CLOSED;
                tcp_pcb_release(pcb);
                mutex_unlock(&mutex);
                return -1;
            }
            pcb->snd.nxt += slen;
            sent += slen;
        }
        break;
    case TCP_PCB_STATE_FIN_WAIT1:
    case TCP_PCB_STATE_FIN_WAIT2:
    case TCP_PCB_STATE_CLOSING:
    case TCP_PCB_STATE_LAST_ACK:
    case TCP_PCB_STATE_TIME_WAIT:
        errorf("connection closing");
        mutex_unlock(&mutex);
        return -1;
    default:
        errorf("unknown state '%u'", pcb->state);
        mutex_unlock(&mutex);
        return -1;
    }
    mutex_unlock(&mutex);
    return sent;
}

ssize_t tcp_receive(int id, uint8_t *buf, size_t size) {
    struct tcp_pcb *pcb;
    size_t remain, len;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
RETRY:
    switch(pcb->state) {
    case TCP_PCB_STATE_CLOSED:
        errorf("connection does not exist");
        mutex_unlock(&mutex);
        return -1;
    case TCP_PCB_STATE_LISTEN:
    case TCP_PCB_STATE_SYN_SENT:
    case TCP_PCB_STATE_SYN_RECEIVED:
        /* ignore: Queue for processing after entering ESTABLISHED state */
        errorf("insufficient resources");
        mutex_unlock(&mutex);
        return -1;
    case TCP_PCB_STATE_ESTABLISHED:
    case TCP_PCB_STATE_FIN_WAIT1:
    case TCP_PCB_STATE_FIN_WAIT2:
        remain = sizeof(pcb->buf) - pcb->rcv.wnd;
        if(!remain) {
            cond_wait(&pcb->cond, &mutex);
            goto RETRY;
        }
        break;
    case TCP_PCB_STATE_CLOSE_WAIT:
        remain = sizeof(pcb->buf) - pcb->rcv.wnd;
        if(remain) { break; }
        /* fall through */
    case TCP_PCB_STATE_CLOSING:
    case TCP_PCB_STATE_LAST_ACK:
    case TCP_PCB_STATE_TIME_WAIT:
        debugf("connection closing");
        mutex_unlock(&mutex);
        return 0;
    default:
        errorf("unknown state '%u'", pcb->state);
        mutex_unlock(&mutex);
        return -1;
    }
    len = MIN(size, remain);
    memcpy(buf, pcb->buf, len);
    memmove(pcb->buf, pcb->buf + len, remain - len);
    pcb->rcv.wnd += len;
    mutex_unlock(&mutex);
    return len;
}

int tcp_close(int id) {
    struct tcp_pcb *pcb;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if(!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
    switch(pcb->state) {
    case TCP_PCB_STATE_CLOSED:
        errorf("connection does not exist");
        mutex_unlock(&mutex);
        return -1;
    case TCP_PCB_STATE_LISTEN:
        pcb->state = TCP_PCB_STATE_CLOSED;
        break;
    case TCP_PCB_STATE_SYN_SENT:
        pcb->state = TCP_PCB_STATE_CLOSED;
        break;
    case TCP_PCB_STATE_SYN_RECEIVED:
        tcp_output(pcb, TCP_FLG_ACK | TCP_FLG_FIN, NULL, 0);
        pcb->snd.nxt++;
        pcb->state = TCP_PCB_STATE_FIN_WAIT1;
        break;
    case TCP_PCB_STATE_ESTABLISHED:
        tcp_output(pcb, TCP_FLG_ACK | TCP_FLG_FIN, NULL, 0);
        pcb->snd.nxt++;
        pcb->state = TCP_PCB_STATE_FIN_WAIT1;
        break;
    case TCP_PCB_STATE_FIN_WAIT1:
    case TCP_PCB_STATE_FIN_WAIT2:
        errorf("connection closing");
        mutex_unlock(&mutex);
        return -1;
    case TCP_PCB_STATE_CLOSE_WAIT:
        tcp_output(pcb, TCP_FLG_ACK | TCP_FLG_FIN, NULL, 0);
        pcb->snd.nxt++;
        pcb->state =
            TCP_PCB_STATE_LAST_ACK; /* RFC793 says "enter CLOSING state", but it
                                       seems to be LAST-ACK state */
        break;
    case TCP_PCB_STATE_CLOSING:
    case TCP_PCB_STATE_LAST_ACK:
    case TCP_PCB_STATE_TIME_WAIT:
        errorf("connection closing");
        mutex_unlock(&mutex);
        return -1;
    default:
        errorf("unknown state '%u'", pcb->state);
        mutex_unlock(&mutex);
        return -1;
    }
    if(pcb->state == TCP_PCB_STATE_CLOSED) {
        tcp_pcb_release(pcb);
    } else {
        cond_broadcast(&pcb->cond);
    }
    mutex_unlock(&mutex);
    return 0;
}
