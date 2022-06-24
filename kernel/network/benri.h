#pragma once
#include "port/connectOS.hpp"
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

#define countof(x) ((sizeof(x) / sizeof(*x)))
#define tailof(x) (x + countof(x))
#define indexof(x, y) (((uintptr_t)y - (uintptr_t)x) / sizeof(*y))

#define div_usec 1000000
#define div_nsec 1000000000

#define timeval_add_usec(x, y)                                                 \
    do {                                                                       \
        (x)->tv_sec += y / div_usec;                                           \
        (x)->tv_usec += y % div_usec;                                          \
        if((x)->tv_usec >= div_usec) {                                         \
            (x)->tv_sec += 1;                                                  \
            (x)->tv_usec -= div_nsec                                           \
        }                                                                      \
    } while(0);

#define timespec_add_nsec(x, y)                                                \
    do {                                                                       \
        (x)->tv_sec += y / div_nsec;                                           \
        (x)->tv_nsec += y % div_nsec;                                          \
        if((x)->tv_nsec >= div_nsec) {                                         \
            (x)->tv_sec += 1;                                                  \
            (x)->tv_nsec -= div_nsec;                                          \
        }                                                                      \
    } while(0);

/**stderr系出力*/
#define errorf(...)                                                            \
    lprintf(stderr, 'E', __FILE__, __LINE__, __func__, __VA_ARGS__)
#define warnf(...)                                                             \
    lprintf(stderr, 'W', __FILE__, __LINE__, __func__, __VA_ARGS__)
#define infof(...)                                                             \
    lprintf(stderr, 'I', __FILE__, __LINE__, __func__, __VA_ARGS__)
#define debugf(...)                                                            \
    lprintf(stderr, 'D', __FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef HEXDUMP
#define debugdump(...) hexdump(stderr, __VA_ARGS__)
#else
#define debugdump(...)
#endif

struct queue_head {
    struct queue_entry *head;
    struct queue_entry *tail;
    unsigned int num;
};

extern void queue_init(struct queue_head *queue);
extern void *queue_push(struct queue_head *queue, void *data);
extern void *queue_pop(struct queue_head *queue);
extern void *queue_peek(struct queue_head *queue);
extern void queue_foreach(struct queue_head *queue,
                          void (*func)(void *arg, void *data), void *arg);

extern uint16_t hton16(uint16_t h);
extern uint16_t ntoh16(uint16_t n);
extern uint32_t hton32(uint32_t h);
extern uint32_t ntoh32(uint32_t n);

extern uint16_t cksum16(uint16_t *addr, uint16_t count, uint32_t init);