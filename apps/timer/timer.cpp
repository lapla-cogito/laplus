#include "../syscall.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void main(int argc, char **argv) {
    if(argc <= 1) {
        printf("Usage: timer <msec>\n");
        exit(1);
    }

    const unsigned long duration_ms = atoi(argv[1]);
    const auto timeout = SyscallCreateTimer(TIMER_ONESHOT_REL, 1, duration_ms);
    printf("timer created. timeout = %lu\n", timeout.value);

    AppEvent events[1];
    while(true) {
        SyscallReadEvent(events, 1);
        if(events[0].type == AppEvent::kTimerTimeout) {
            printf("%lu milisecs elapsed!\n", duration_ms);
            break;
        } else {
            printf("unknown event happened: type = %d\n", events[0].type);
        }
    }
    exit(0);
}