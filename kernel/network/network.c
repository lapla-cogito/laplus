/**
 * @file network.c
 *
 * @brief ネットワーク機能の包括的処理
 */
#include "network.h"
#include "arp.h"
#include "benri.h"
#include "icmp.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/**プロトコル構造体*/
struct net_protocol {
    struct net_protocol *next; /**次のプロトコルへのポインタ*/
    uint16_t type;             /**プロトコルタイプ*/
    mutex_t mutex;             /* mutex for input queue */
    struct queue_head queue;   /**受信キュー*/
    void (*handler)(const uint8_t *data, size_t len,
                    struct net_device *dev); /**プロトコル入力関数へのポインタ*/
};

/**受信キューのエントリ構造体*/
struct net_protocol_queue_entry {
    struct net_device *dev;
    size_t len;
};

struct net_timer {
    struct net_timer *next;
    struct timeval interval;
    struct timeval last;
    void (*handler)(void);
};

/* NOTE: if you want to add/delete the entries after net_run(), you need to
 * protect these lists with a mutex. */
/**デバイスリスト(の先頭を指すポインタ)*/
static struct net_device *devices;
/**プロトコルリスト(の先頭を指すポインタ)*/
static struct net_protocol *protocols;
static struct net_timer *timers;

/**デバイス構造体のサイズのメモリ確保*/
struct net_device *net_device_alloc(void) {
    struct net_device *dev;

    dev = (struct net_device *)calloc(1, sizeof(*dev));
    if(!dev) {
        errorf("calloc() in net_device_alloc() failure");
        return NULL;
    }
    return dev;
}

/**ネットワークデバイスの登録*/
int net_device_register(struct net_device *dev) {
    static unsigned int index = 0;
    /**インデックス設定*/
    dev->index = index++;
    /**デバイス名設定*/
    snprintf(dev->name, sizeof(dev->name), "net%d", dev->index);
    /**デバイスリスト先頭に*/
    dev->next = devices;
    devices = dev;
    infof("registered, dev=%s, type=0x%04x", dev->name, dev->type);
    return 0;
}

/**デバイスをopen*/
int net_device_open(struct net_device *dev) {
    /**既に開いていないか*/
    if(NET_DEVICE_IS_UP(dev)) {
        errorf("device is already opened, dev=%s", dev->name);
        return -1;
    }
    if(dev->ops->open) {
        if(dev->ops->open(dev) == -1) {
            errorf("device open failure, dev=%s", dev->name);
            return -1;
        }
    }
    dev->flags |= NET_DEVICE_FLAG_UP;
    infof("successfully opened, dev=%s, state=%s", dev->name,
          NET_DEVICE_STATE(dev));
    return 0;
}

/**デバイスをclose*/
int net_device_close(struct net_device *dev) {
    /**既に閉じていないか*/
    if(!NET_DEVICE_IS_UP(dev)) {
        errorf("device is not opened, dev=%s", dev->name);
        return -1;
    }
    if(dev->ops->close) {
        if(dev->ops->close(dev) == -1) {
            errorf("device close failure, dev=%s", dev->name);
            return -1;
        }
    }
    dev->flags &= ~NET_DEVICE_FLAG_UP;
    infof("successfully closed, dev=%s, state=%s", dev->name,
          NET_DEVICE_STATE(dev));
    return 0;
}

struct net_device *net_device_by_index(unsigned int index) {
    struct net_device *entry;

    for(entry = devices; entry; entry = entry->next) {
        if(entry->index == index) { break; }
    }
    return entry;
}
struct net_device *net_device_by_name(const char *name) {
    struct net_device *entry;

    for(entry = devices; entry; entry = entry->next) {
        if(strcmp(entry->name, name) == 0) { break; }
    }
    return entry;
}

/**デバイスとインターフェースをリンク*/
int net_device_add_iface(struct net_device *dev, struct net_iface *iface) {
    struct net_iface *entry;
    /**重複登録(1デバイスにどうファミリーのインターフェースが無いか?)*/
    for(entry = dev->ifaces; entry; entry = entry->next) {
        if(entry->family == iface->family) {
            errorf("device already exists, dev=%s, family=%d", dev->name,
                   entry->family);
            return -1;
        }
    }
    iface->next = dev->ifaces;
    iface->dev = dev;
    dev->ifaces = iface;
    return 0;
}

/**デバイスにリンクしているインターフェースを探して返す*/
struct net_iface *net_device_get_iface(struct net_device *dev, int family) {
    struct net_iface *entry;

    for(entry = dev->ifaces; entry; entry = entry->next) {
        if(entry->family == family) { break; }
    }
    return entry;
}

/**デバイスへの出力*/
int net_device_output(struct net_device *dev, uint16_t type,
                      const uint8_t *data, size_t len, const void *dst) {
    /**openしているか*/
    if(!NET_DEVICE_IS_UP(dev)) {
        errorf("not opened, dev=%s", dev->name);
        return -1;
    }
    /**送信データがMTUを超えていないか*/
    if(len > dev->mtu) {
        errorf("too long, dev=%s, mtu=%u, len=%lu", dev->name, dev->mtu, len);
        return -1;
    }
    /**出力*/
    debugf("dev=%s, type=0x%04x, len=%lu", dev->name, type, len);
    debugdump(data, len);
    if(dev->ops->transmit(dev, type, data, len, dst) == -1) {
        errorf("device transmit failure, dev=%s, len=%lu", dev->name, len);
        return -1;
    }
    return 0;
}

/**デバイスが受信したパケットをプロトコルスタックに*/
int net_input_handler(uint16_t type, const uint8_t *data, size_t len,
                      struct net_device *dev) {
    struct net_protocol *proto;
    struct net_protocol_queue_entry *entry;

    for(proto = protocols; proto; proto = proto->next) {
        if(proto->type == type) {
            /**プロトコルの受信キューにエントリを挿入する*/
            /**size分のメモリ確保*/
            entry = (struct net_protocol_queue_entry *)calloc(
                1, sizeof(*entry) + len);
            if(!entry) {
                errorf("calloc()  in net_input_handler() failure");
                return -1;
            }
            /**エントリメタデータ設定*/
            entry->dev = dev;
            entry->len = len;
            memcpy(entry + 1, data, len);
            mutex_lock(&proto->mutex);
            /**挿入*/
            if(!queue_push(&proto->queue, entry)) {
                mutex_unlock(&proto->mutex);
                errorf("queue_push() in net_input_handler() failure");
                free(entry);
                return -1;
            }
            debugf("queue pushed (num: %u), dev=%s, type=0x%04x, len=%ld",
                   proto->queue.num, dev->name, type, len);
            debugdump(data, len);
            mutex_unlock(&proto->mutex);
            softirq(); /**エントリ追加後に割り込み発生*/
            return 0;
        }
    }
    /* unsupported protocol */
    return 0;
}

/**プロトコル登録*/
int net_protocol_register(uint16_t type,
                          void (*handler)(const uint8_t *data, size_t len,
                                          struct net_device *dev)) {
    struct net_protocol *proto;

    /**重複登録の確認*/
    for(proto = protocols; proto; proto = proto->next) {
        if(type == proto->type) {
            errorf("already registered, type=0x%04x", type);
            return -1;
        }
    }

    /**プロトコル構造体のサイズ分のメモリ確保*/
    proto = (struct net_protocol *)calloc(1, sizeof(*proto));
    if(!proto) {
        errorf("calloc() in net_protocol_register() failure");
        return -1;
    }
    proto->type = type;
    mutex_init(&proto->mutex, NULL);
    proto->handler = handler;

    /**プロトコルリスト先頭に*/
    proto->next = protocols;
    protocols = proto;
    infof("registered, type=0x%04x", type);
    return 0;
}

/* NOTE: must not be call after net_run() */
int net_timer_register(struct timeval interval, void (*handler)(void)) {
    struct net_timer *timer;

    timer = (struct net_timer *)calloc(1, sizeof(*timer));
    if(!timer) {
        errorf("calloc() in net_timer_register() failure");
        return -1;
    }
    timer->interval = interval;
    gettimeofday(&timer->last, NULL);
    timer->handler = handler;
    timer->next = timers;
    timers = timer;
    infof("registered: interval={%d, %d}", interval.tv_sec, interval.tv_usec);
    return 0;
}

/**ソフトウェア割り込み発生時*/
void net_softirq_handler() {
    struct net_protocol *proto;
    struct net_protocol_queue_entry *entry;

    /**各プロトコルを確認*/
    for(proto = protocols; proto; proto = proto->next) {
        mutex_lock(&proto->mutex);
        entry = (struct net_protocol_queue_entry *)queue_pop(&proto->queue);
        if(!entry) {
            mutex_unlock(&proto->mutex);
            continue;
        }
        debugf("queue popped (num:%u), dev=%s, type=0x%04x, len=%ld",
               proto->queue.num, entry->dev->name, proto->type, entry->len);
        debugdump((uint8_t *)(entry + 1), entry->len);
        mutex_unlock(&proto->mutex);
        proto->handler((uint8_t *)(entry + 1), entry->len, entry->dev);
        /**メモリ解放*/
        free(entry);
    }
}

void net_timer_handler() {
    struct net_timer *timer;
    struct timeval now, diff;

    for(timer = timers; timer; timer = timer->next) {
        gettimeofday(&now, NULL);
        timersub(&now, &timer->last, &diff);
        if(timercmp(&timer->interval, &diff, <) !=
           0) { /* true (!0) or false (0) */
            timer->handler();
            timer->last = now;
        }
    }
}

/**登録済みデバイスを全open*/
int net_run() {
    struct net_device *dev;

    debugf("open all devices...");
    for(dev = devices; dev; dev = dev->next) { net_device_open(dev); }
    debugf("running...");
    return 0;
}

/**登録済みデバイスを全close*/
void net_shutdown() {
    struct net_device *dev;

    debugf("close all devices...");
    for(dev = devices; dev; dev = dev->next) { net_device_close(dev); }
    debugf("shutdown");
}

void net_interrupt() {
    // ignore
}

int net_init() {
    /**
     * @brief 各種プロトコルの初期化を行う
     */

    if(arp_init() == -1) {
        errorf("arp_init() failure");
        return -1;
    }
    if(ip_init() == -1) {
        errorf("ip_init() failure");
        return -1;
    }
    if(icmp_init() == -1) {
        errorf("icmp_init() failure");
        return -1;
    }
    if(udp_init() == -1) {
        errorf("udp_init() failure");
        return -1;
    }
    if(tcp_init() == -1) {
        errorf("tcp_init() failure");
        return -1;
    }
    infof("Successfully initialized");
    return 0;
}