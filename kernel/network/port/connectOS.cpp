#include "interrupt.hpp"
#include "logger.hpp"
#include "network/benri.h"
#include "network/nic/e1000.hpp"
#include "pci.hpp"
#include "task.hpp"
#include "timer.hpp"
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <sys/time.h>

void flockfile(FILE *fp) {
    // pass
}

void funlockfile(FILE *fp) {
    // pass
}

int gettimeofday(struct timeval *tv, void *tz) {
    unsigned long tick = timer_manager->CurrentTick();

    tv->tv_sec = tick / kTimerFreq;
    tv->tv_usec = tick % kTimerFreq * (1000000 / kTimerFreq);
    return 0;
}

int lprintf(FILE *fp, int level, const char *file, int line, const char *func,
            const char *fmt, ...) {
    struct timeval tv;
    struct tm tm;
    char timestamp[32];
    char buf[1024];
    int n = 0;
    va_list ap;

    gettimeofday(&tv, NULL);
    strftime(timestamp, sizeof(timestamp), "%T", localtime_r(&tv.tv_sec, &tm));
    n += sprintf(buf, "%s.%03d [%c] %s: ", timestamp, (int)(tv.tv_usec / 1000),
                 level, func);
    va_start(ap, fmt);
    n += vsprintf(buf + n, fmt, ap);
    va_end(ap);
    n += sprintf(buf + n, " (%s:%d)\n", file, line);
    flockfile(fp);
    Log(kWarn, "%s", buf);
    funlockfile(fp);
    return n;
}

void hexdump(FILE *fp, const void *data, size_t size) {
    unsigned char *src;
    int offset, index;

    flockfile(fp);
    src = (unsigned char *)data;
    fprintf(fp, "+------+-------------------------------------------------+----"
                "--------------+\n");
    for(offset = 0; offset < (int)size; offset += 16) {
        fprintf(fp, "| %04x | ", offset);
        for(index = 0; index < 16; index++) {
            if(offset + index < (int)size) {
                fprintf(fp, "%02x ", 0xff & src[offset + index]);
            } else {
                fprintf(fp, "   ");
            }
        }
        fprintf(fp, "| ");
        for(index = 0; index < 16; index++) {
            if(offset + index < (int)size) {
                if(isascii(src[offset + index]) &&
                   isprint(src[offset + index])) {
                    fprintf(fp, "%c", src[offset + index]);
                } else {
                    fprintf(fp, ".");
                }
            } else {
                fprintf(fp, " ");
            }
        }
        fprintf(fp, " |\n");
    }
    fprintf(fp, "+------+-------------------------------------------------+----"
                "--------------+\n");
    funlockfile(fp);
}

int mutex_init(mutex_t *mutex, const void *attr) { return 0; }

int mutex_lock(mutex_t *mutex) { return 0; }

int mutex_unlock(mutex_t *mutex) { return 0; }

int cond_init(cond_t *cond, const void *attr) {
    cond->taskid = 0;
    cond->num = 0;
    return 0;
}

int cond_wait(cond_t *cond, mutex_t *mutex) {
    cond->taskid = task_manager->CurrentTask().ID();
    cond->num++;
    task_manager->Sleep(cond->taskid);
    cond->num--;
    return 0;
}

int cond_broadcast(cond_t *cond) {
    task_manager->Wakeup(cond->taskid);
    return 0;
}

int cond_destroy(cond_t *cond) {
    if(cond->num) { return EBUSY; }
    cond->taskid = 0;
    cond->num = 0;
    return 0;
}

void softirq(void) {
    task_manager->SendMessage(1, Message{Message::kNetInput});
}

void e1000_probe(void) {
    for(int i = 0; i < pci::num_device; ++i) {
        if(pci::ReadVendorId(pci::devices[i]) != 0x8086 ||
           pci::ReadDeviceId(pci::devices[i]) != 0x10d3) {
            continue;
        }
        pci::Device *dev = &pci::devices[i];
        debugf("found: %d.%d.%d", dev->bus, dev->device, dev->function);
        // MMIO Base Address
        const WithError<uint64_t> bar = pci::ReadBar(*dev, 0);
        uintptr_t mmio_base = bar.value & ~static_cast<uint64_t>(0xf);
        debugf("mmio_base = 0x%08x", mmio_base);
        // Register MSI
        const uint8_t bsp_local_apic_id =
            *reinterpret_cast<const uint32_t *>(0xfee00020) >> 24;
        pci::ConfigureMSIFixedDestination(
            *dev, bsp_local_apic_id, pci::MSITriggerMode::kLevel,
            pci::MSIDeliveryMode::kFixed, InterruptVector::kE1000, 0);
        debugf("msi registered, irq = %d", InterruptVector::kE1000);
        e1000_init(mmio_base);
    }
}