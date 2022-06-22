// ifconfig
#include "../syscall.h"
#include <cstdio>
#include <cstdlib>

static void disp_al() {
    int mon;
    const char **s, *str[] = {
        "UP",
        "BROADCAST",
        "DEBUG",
        "POINTTOPOINT",
        "LOOPBACK",
        "PROMISC",
        "ALLMULTI",
        "MULTICAST",
        NULL
    }
}

extern "C" void main(int argc, char **argv) {
    if(argc == 1) {
        disp_al();
        exit(0);
    } else if(argc == 2) {

    } else if(argc == 3) {

    } else if(argc == 5) {

    } else {
        /*usage表示*/
        fprintf(stderr, "Usage: %s interface [command|address]\n", argv[0]);
        fprintf(stderr, "        -command: up | down\n");
        fprintf(stderr,
                "        -address: ADDRESS/PREFIX | ADDRESS netmask NETMASK\n");
        fprintf(stderr, "%s [-a]\n", argv[0]);
        exit(1);
    }
}