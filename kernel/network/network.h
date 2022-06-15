#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

#define IRQ_SHARED 0x0001

#define DEVICE_TYPE_NULL 0x0000
#define DEVICE_TYPE_LOOPBACK 0x0001
#define DEVICE_TYPE_ETHERNET 0x0002

#define DEVICE_FLAG_UP 0x0001
#define DEVICE_FLAG_LOOPBACK 0x0010
#define DEVICE_FLAG_BROADCAST 0x0020
#define DEVICE_FLAG_P2P 0x0040
#define DEVICE_FLAG_NEED_ARP 0x0100

#define DEVICE_ADDR_LEN 16

#define DEVICE_IS_UP(x) ((x)->flags & DEVICE_FLAG_UP)
#define DEVICE_STATE(x) (DEVICE_IS_UP(x) ? "up" : "down")

#define IFACE_FAMILY_IP 2
#define IFACE_FAMILY_IPV6 10

#define IFACE(x) ((struct iface *)(x))

#define PROTOCOL_TYPE_IP 0x0800
#define PROTOCOL_TYPE_ARP 0x0806
#define NTT_PROTOCOL_TYPE_IPV6 0x86dd

struct device {
    struct device *next;
    struct iface *ifaces;
    unsigned int index;
    char name[IFNAMSIZ];
    uint16_t type;
    uint16_t mtu;
    uint16_t flags;
    uint16_t hlen; //ヘッダー長
    uint16_t alen; //アドレス長
    uint8_t addr[DEVICE_ADDR_LEN];
    union {
        uint8_t peer[DEVICE_ADDR_LEN];
        uint8_t broadcast[DEVICE_ADDR_LEN];
    };
    struct device_ops *ops;
    void *priv;
};

struct device_ops {
    int (*open)(struct device *dev);
    int (*close)(struct device *dev);
    int (*transmit)(struct device *dev, uint16_t type, const uint8_t *data,
                    size_t len, const void *dst);
};

struct iface {
    struct iface *next;
    struct device *devi;
    int family;
};

extern struct device *device_alloc();
extern int device_register(struct device *devi);
extern int device_open(struct device *devi);
extern int device_close(struct device *devi);
struct device *device_by_index(unsigned int index);
extern struct device *device_by_name(const char *name);
extern int device_add_iface(struct device *devi, struct iface *iface);
extern struct iface *device_get_iface(struct device *devi, int family);
extern int device_output(struct device *devi, uint16_t type,
                         const uint8_t *data, size_t len, const void *dst);

extern int input_handler(uint16_t type, const uint8_t *data, size_t len,
                         struct device *devi);

extern int protocol_register(uint16_t type,
                             void (*handler)(const uint8_t *data, size_t len,
                                             struct device *devi));
extern int timer_register(struct timeval interval, void (*handler)());

extern void softirq_handler();
extern void timer_handler();

extern int run();
extern void shutdown();
extern void interrupt();
extern int init();

#ifdef __cplusplus
}
#endif