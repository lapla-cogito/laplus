#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "network.h"

struct protocol {
    struct protocol *next;
    uint16_t type;
    mutex_t mutex;
    struct queue_head queue;
    void (*handler)(const uint8_t *data, size_t len, struct device *dev);
};

struct timer {
    struct timer *next;
    struct timeval interval, last;
    void (*handler)();
};

static struct device *devices;
static struct protocol *protocols;
static struct timer *timers;

struct device *alloc() {
    struct device *devi;
    devi = (struct device *)calloc(1, sizeof(*devi));

    if(!devi) { return NULL; }
    return devi;
}

int device_regi(struct device *devi) {
    static unsigned int ind = 0;
    devi->index = ++ind;
    devi->next = devices;
    devices = devi;
    return 0;
}