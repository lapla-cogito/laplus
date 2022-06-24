/**
 * @file benri.c
 *
 * @brief 便利
 */
#include "benri.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

struct queue_entry {
    struct queue_entry *next;
    void *data;
};

void queue_init(struct queue_head *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->num = 0;
}

void *queue_push(struct queue_head *queue, void *data) {
    struct queue_entry *entry;

    if(!queue) { return NULL; }
    entry = (struct queue_entry *)calloc(1, sizeof(*entry));
    if(!entry) { return NULL; }
    entry->next = NULL;
    entry->data = data;
    if(queue->tail) { queue->tail->next = entry; }
    queue->tail = entry;
    if(!queue->head) { queue->head = entry; }
    queue->num++;
    return data;
}

void *queue_pop(struct queue_head *queue) {
    struct queue_entry *entry;
    void *data;

    if(!queue || !queue->head) { return NULL; }
    entry = queue->head;
    queue->head = entry->next;
    if(!queue->head) { queue->tail = NULL; }
    queue->num--;
    data = entry->data;
    free(entry);
    return data;
}

void *queue_peek(struct queue_head *queue) {
    if(!queue || !queue->head) { return NULL; }
    return queue->head->data;
}

void queue_foreach(struct queue_head *queue,
                   void (*func)(void *arg, void *data), void *arg) {
    struct queue_entry *entry;

    if(!queue || !func) { return; }
    for(entry = queue->head; entry; entry = entry->next) {
        func(arg, entry->data);
    }
}

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif

static int endian;

static int byteorder(void) {
    uint32_t x = 0x00000001;

    return *(uint8_t *)&x ? __LITTLE_ENDIAN : __BIG_ENDIAN;
}

static uint16_t byteswap16(uint16_t v) {
    return (v & 0x00ff) << 8 | (v & 0xff00) >> 8;
}

static uint32_t byteswap32(uint32_t v) {
    return (v & 0x000000ff) << 24 | (v & 0x0000ff00) << 8 |
           (v & 0x00ff0000) >> 8 | (v & 0xff000000) >> 24;
}

uint16_t hton16(uint16_t h) {
    if(!endian) { endian = byteorder(); }
    return endian == __LITTLE_ENDIAN ? byteswap16(h) : h;
}

uint16_t ntoh16(uint16_t n) {
    if(!endian) { endian = byteorder(); }
    return endian == __LITTLE_ENDIAN ? byteswap16(n) : n;
}

uint32_t hton32(uint32_t h) {
    if(!endian) { endian = byteorder(); }
    return endian == __LITTLE_ENDIAN ? byteswap32(h) : h;
}

uint32_t ntoh32(uint32_t n) {
    if(!endian) { endian = byteorder(); }
    return endian == __LITTLE_ENDIAN ? byteswap32(n) : n;
}

uint16_t cksum16(uint16_t *addr, uint16_t count, uint32_t init) {
    uint32_t sum;

    sum = init;
    while(count > 1) {
        sum += *(addr++);
        count -= 2;
    }
    if(count > 0) { sum += *(uint8_t *)addr; }
    while(sum >> 16) { sum = (sum & 0xffff) + (sum >> 16); }
    return ~(uint16_t)sum;
}