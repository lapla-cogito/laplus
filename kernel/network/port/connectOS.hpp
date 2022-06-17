#pragma once

#ifdef __cplusplus
extern "C" 
{
#endif

#include <stdio.h>

extern int
printk(const char* format, ...);

extern int
lprintf(FILE *fp, int level, const char *file, int line, const char *func, const char *fmt, ...);
extern void
hexdump(FILE *fp, const void *data, size_t size);

#define MUTEX_INITIALIZER {}

typedef struct {
    uint8_t _unused;
} mutex_t;

extern int
mutex_init(mutex_t *mutex, const void *attr);
extern int
mutex_lock(mutex_t *mutex);
extern int
mutex_unlock(mutex_t *mutex);

#define COND_INITIALIZER {}

typedef struct {
    uint64_t taskid;
    uint64_t num;
} cond_t;

extern int
cond_init(cond_t *cond, const void *attr);
extern int
cond_wait(cond_t *cond, mutex_t *mutex);
extern int
cond_broadcast(cond_t *cond);
extern int
cond_destroy(cond_t *cond);

extern void
softirq(void);
extern void
e1000_probe(void);

#ifdef __cplusplus
}
#endif