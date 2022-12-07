/**
 * @file e1000.cpp
 *
 * @brief E1000のNICドライバの実装
 *
 * @note E1000はエミュレートバージョン(実機でも仮想マシンでも動く)ので都合が良い
 */
#include "e1000.hpp"
#include "../benri.h"
#include "../ethernet.h"
#include "../network.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RX_RING_SIZE 16
#define TX_RING_SIZE 16

struct e1000 {
    uintptr_t mmio_base;
    struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
    struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
    uint8_t addr[6];
    struct net_device *dev;
    struct e1000 *next;
};

struct e1000 *adapters;

static unsigned int e1000_reg_read(struct e1000 *adapter, uint16_t reg) {
    return *(volatile uint32_t *)(adapter->mmio_base + reg);
}

static void e1000_reg_write(struct e1000 *adapter, uint16_t reg, uint32_t val) {
    *(volatile uint32_t *)(adapter->mmio_base + reg) = val;
}

/**e1000ドライバを使用準備*/
static int e1000_open(struct net_device *dev) {
    struct e1000 *adapter = (struct e1000 *)dev->priv;
    // enable interrupts
    e1000_reg_write(adapter, E1000_IMS, E1000_IMS_RXT0);
    // clear existing pending interrupts
    e1000_reg_read(adapter, E1000_ICR);
    // enable RX/TX
    e1000_reg_write(adapter, E1000_RCTL,
                    e1000_reg_read(adapter, E1000_RCTL) | E1000_RCTL_EN);
    e1000_reg_write(adapter, E1000_TCTL,
                    e1000_reg_read(adapter, E1000_TCTL) | E1000_TCTL_EN);
    // link up
    e1000_reg_write(adapter, E1000_CTL,
                    e1000_reg_read(adapter, E1000_CTL) | E1000_CTL_SLU);
    return 0;
}

/**e1000ドライバをclose*/
static int e1000_close(struct net_device *dev) {
    struct e1000 *adapter = (struct e1000 *)dev->priv;
    // disable interrupts
    e1000_reg_write(adapter, E1000_IMC, E1000_IMS_RXT0);
    // clear existing pending interrupts
    e1000_reg_read(adapter, E1000_ICR);
    // disable RX/TX
    e1000_reg_write(adapter, E1000_RCTL,
                    e1000_reg_read(adapter, E1000_RCTL) & ~E1000_RCTL_EN);
    e1000_reg_write(adapter, E1000_TCTL,
                    e1000_reg_read(adapter, E1000_TCTL) & ~E1000_TCTL_EN);
    // link down
    e1000_reg_write(adapter, E1000_CTL,
                    e1000_reg_read(adapter, E1000_CTL) & ~E1000_CTL_SLU);
    return 0;
}

static void e1000_rx_init(struct e1000 *adapter) {
    // initialize rx descriptors
    for(int n = 0; n < RX_RING_SIZE; n++) {
        memset(&adapter->rx_ring[n], 0, sizeof(struct rx_desc));
        // alloc DMA buffer
        adapter->rx_ring[n].addr = (uint64_t)calloc(1, 2048);
    }
    // setup rx descriptors
    uint64_t base = (uint64_t)(adapter->rx_ring);
    e1000_reg_write(adapter, E1000_RDBAL, (uint32_t)(base & 0xffffffff));
    e1000_reg_write(adapter, E1000_RDBAH, (uint32_t)(base >> 32));
    // rx descriptor lengh
    e1000_reg_write(adapter, E1000_RDLEN,
                    (uint32_t)(RX_RING_SIZE * sizeof(struct rx_desc)));
    // setup head/tail
    e1000_reg_write(adapter, E1000_RDH, 0);
    e1000_reg_write(adapter, E1000_RDT, RX_RING_SIZE - 1);
    // set tx control register
    e1000_reg_write(adapter, E1000_RCTL,
                    (E1000_RCTL_SBP |        /** store bad packet */
                     E1000_RCTL_UPE |        /** unicast promiscuous enable */
                     E1000_RCTL_MPE |        /** multicast promiscuous enable */
                     E1000_RCTL_RDMTS_HALF | /** rx desc min threshold size */
                     E1000_RCTL_SECRC |      /** Strip Ethernet CRC */
                     E1000_RCTL_LPE |        /** long packet enable */
                     E1000_RCTL_BAM |        /** broadcast enable */
                     E1000_RCTL_SZ_2048 |    /** rx buffer size 2048 */
                     0));
}

static void e1000_tx_init(struct e1000 *adapter) {
    // initialize tx descriptors
    for(int n = 0; n < TX_RING_SIZE; n++) {
        memset(&adapter->tx_ring[n], 0, sizeof(struct tx_desc));
    }
    // setup tx descriptors
    uint64_t base = (uint64_t)(adapter->tx_ring);
    e1000_reg_write(adapter, E1000_TDBAL, (uint32_t)(base & 0xffffffff));
    e1000_reg_write(adapter, E1000_TDBAH, (uint32_t)(base >> 32));
    // tx descriptor length
    e1000_reg_write(adapter, E1000_TDLEN,
                    (uint32_t)(TX_RING_SIZE * sizeof(struct tx_desc)));
    // setup head/tail
    e1000_reg_write(adapter, E1000_TDH, 0);
    e1000_reg_write(adapter, E1000_TDT, 0);
    // set tx control register
    e1000_reg_write(adapter, E1000_TCTL,
                    (E1000_TCTL_PSP | /* pad short packets */
                     0));
}

static ssize_t e1000_write(struct net_device *dev, const uint8_t *data,
                           size_t len) {
    struct e1000 *adapter = (struct e1000 *)dev->priv;
    uint32_t tail = e1000_reg_read(adapter, E1000_TDT);
    struct tx_desc *desc = &adapter->tx_ring[tail];

    desc->addr = (uintptr_t)data;
    desc->length = len;
    desc->status = 0;
    desc->cmd = (E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS);
    debugf("%s: %u bytes data transmit", adapter->dev->name, desc->length);
    e1000_reg_write(adapter, E1000_TDT, (tail + 1) % TX_RING_SIZE);
    while(!(desc->status & 0x0f))
        ; // busy wait
    return len;
}

static int e1000_transmit(struct net_device *dev, uint16_t type,
                          const uint8_t *packet, size_t len, const void *dst) {
    return ether_transmit_helper(dev, type, packet, len, dst, e1000_write);
}

static void e1000_receive(struct e1000 *adapter) {
    debugf("%s: check rx descriptors...", adapter->dev->name);
    while(1) {
        uint32_t tail = (e1000_reg_read(adapter, E1000_RDT) + 1) % RX_RING_SIZE;
        struct rx_desc *desc = &adapter->rx_ring[tail];
        if(!(desc->status & E1000_RXD_STAT_DD)) {
            // FIXME: workaround
            e1000_reg_write(adapter, E1000_IMC, E1000_IMS_RXT0);
            e1000_reg_read(adapter, E1000_ICR);
            e1000_reg_write(adapter, E1000_IMS, E1000_IMS_RXT0);
            break;
        }

        do {
            if(desc->length < 60) {
                errorf("%s: short packet (%d bytes)", adapter->dev->name,
                       desc->length);
                break;
            }
            if(!(desc->status & E1000_RXD_STAT_EOP)) {
                errorf("%s: not EOP! this driver does not support packet that "
                       "do not fit in one buffer",
                       adapter->dev->name);
                break;
            }
            if(desc->errors) {
                errorf("%s: rx errors (0x%x)", adapter->dev->name,
                       desc->errors);
                break;
            }
            debugf("%s: %u bytes data received", adapter->dev->name,
                   desc->length);
            ether_input((uint8_t *)desc->addr, desc->length, adapter->dev);
        } while(0);
        desc->status = (uint16_t)(0);
        e1000_reg_write(adapter, E1000_RDT, tail);
    }
}

void e1000_intr(void) {
    struct e1000 *adapter;
    int icr;

    for(adapter = adapters; adapter; adapter = adapter->next) {
        icr = e1000_reg_read(adapter, E1000_ICR);
        if(icr & E1000_ICR_RXT0) {
            e1000_receive(adapter);
            e1000_reg_read(adapter, E1000_ICR);
        }
    }
}

static struct e1000 *e1000_alloc(uintptr_t mmio_base) {
    struct e1000 *adapter;
    uint32_t v1, v2;

    adapter = (struct e1000 *)calloc(1, sizeof(*adapter));
    if(!adapter) {
        errorf("calloc() failure");
        return NULL;
    }

    adapter->mmio_base = mmio_base;
    v1 = e1000_reg_read(adapter, 0x5400);
    v2 = e1000_reg_read(adapter, 0x5404);
    adapter->addr[0] = ((uint8_t *)&v1)[0];
    adapter->addr[1] = ((uint8_t *)&v1)[1];
    adapter->addr[2] = ((uint8_t *)&v1)[2];
    adapter->addr[3] = ((uint8_t *)&v1)[3];
    adapter->addr[4] = ((uint8_t *)&v2)[0];
    adapter->addr[5] = ((uint8_t *)&v2)[1];
    debugf("mmio_base = %0x08x, addr = %02x:%02x:%02x:%02x:%02x:%02x",
           adapter->mmio_base, adapter->addr[0], adapter->addr[1],
           adapter->addr[2], adapter->addr[3], adapter->addr[4],
           adapter->addr[5]);
    for(int n = 0; n < 128; ++n) {
        e1000_reg_write(adapter, E1000_MTA + (n << 2), 0);
    }
    e1000_rx_init(adapter);
    e1000_tx_init(adapter);
    return adapter;
}

static struct net_device_ops e1000_ops = {
    .open = e1000_open,
    .close = e1000_close,
    .transmit = e1000_transmit,
};

int e1000_init(uintptr_t mmio_base) {
    struct e1000 *adapter;
    struct net_device *dev;

    adapter = e1000_alloc(mmio_base);
    if(!adapter) {
        errorf("e1000_alloc() failure");
        return -1;
    }
    dev = net_device_alloc();
    if(!dev) {
        errorf("net_device_alloc() failure");
        return -1;
    }
    ether_setup_helper(dev);
    memcpy(dev->addr, adapter->addr, 6);
    dev->priv = adapter;
    dev->ops = &e1000_ops;
    if(net_device_register(dev) == -1) {
        errorf("net_device_register() failure");
        return -1;
    }
    adapter->dev = dev;
    adapter->next = adapters;
    adapters = adapter;
    debugf("initialized");
    return 0;
}
