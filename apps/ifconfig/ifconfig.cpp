// ifconfig
#include "../syscall.h"
#include <cstdio>
#include <cstdlib>
#include <stdio.h>
#include <string.h>

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

static void ifup(const char *name) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;

    if(fd == -1) { return; }

    strcpy(ifr.ifr_name, name);
    if(ioctl(fd, SIOCGIFFLAGS, &ifr) == -1) {
        close(fd);
        printf("ifconfig: interface %s doen not exist\n", name);
        return;
    }

    ifr.ifr_flags |= IFF_UP;
    if(ioctl(fd, SIOCGIFFLAGS, &ifr) == -1) {
        close(fd);
        printf("ifconfig: ioctl() failre at interface %s\n", name);
        return;
    }

    close(fd);
}

static void ifdown(const char *name) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;

    if(fd == -1) { return; }

    strcpy(ifr.ifr_name, name);
    if(ioctl(fd, SIOCGIFFLAGS, &ifr) == -1) {
        close(fd);
        printf("ifconfig: interface %s doen not exist\n", name);
        return;
    }

    ifr.ifr_flags &= ~IFF_UP;
    if(ioctl(fd, SIOCGIFFLAGS, &ifr) == -1) {
        close(fd);
        printf("ifconfig: ioctl() failre at interface %s\n", name);
        return;
    }

    close(fd);
}

static void ifset(const char *name, ip_addr_t *addr, ip_addr_t *netmask) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;

    if(fd == -1) { return; }

    strcpy(ifr.ifr_name, name);

    ifr.ifr_addr.sa_family = AF_INET;

    ((struct socketaddr_in *)&ifr.ifr_addr)->sin_addr = *addr;

    if(ioctl(fd, SIOCSIFADDR, &ifr) == -1) {
        close(fd);
        printf("ifconfig: ioctl() failre at interface %s\n", name);
        return;
    }

    ifr.ifr_netmask.sa_family = AF_INET;
    ((struct socketaddr_in *)&ifr.ifr_netmask)->sin_addr = *netmask;

    if(ioctl(fd, SIOCSIFADDR, &ifr) == -1) {
        close(fd);
        printf("ifconfig: ioctl() failre at interface %s\n", name);
        return;
    }

    close(fd);
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