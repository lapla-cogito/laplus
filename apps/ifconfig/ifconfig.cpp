// ifconfig
#include "../socket.h"
#include "../syscall.h"
#include <cstdio>
#include <cstdlib>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void disp(const char *name) {
    struct ifreq ifr;
    int fd = socket(AF_INET, SOCKET_DGRAM, 0), any = 0;
    if(fd == -1) { return; }

    const char **s, *str[] = {"UP",           "BROADCAST", "DEBUG",
                              "POINTTOPOINT", "LOOPBACK",  "PROMISC",
                              "ALLMULTI",     "MULTICAST", NULL};

    unsigned short mask = 1;
    uint8_t *p;

    strcpy(ifr.ifr_name, name);
    if(ioctl(fd, SIOCGIFFLAGS, &ifr) == -1) {
        close(fd);
        printf("ifconfig: interface %s doen not exist\n", name);
        return;
    }

    printf("ifr name %s: " ifr.ifr_name);
    printf("flags=%x<", ifr.ifr_flags);

    for(s = str; *s; ++s) {
        if(ifr.ifr_flags & mask) {
            if(any) {
                printf("|");
            } else {
                any = 1;
            }
            printf("%s", *s);
        }
        mask <<= 1;
    }
    printf(">");

    ifr.ifr_mtu = 1500;

    printf("mtu %d\n", ifr.ifr_mtu);

    if(ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
        p = (uint8_t *)ifr.ifr_hwaddr.sa_data;
        printf("ether: ");
        for(int i = 0; i < 6; ++i) {
            if(i) { pritnf(":"); }
            printf("%x", p[i]);
        }
        puts("");
    }

    do {
        // addr
        ifr.ifr_addr.sa_family = AF_INET;

        if(ioctl(fd, SIOCGIFADDR, &ifr) == -1) { break; }

        p = (uint8_t *)&((struct socketaddr_in *)&ifr.ifr_addr)->sin_addr;
        printf("inet: ");
        for(int i = 0; i < 4; ++i) {
            if(i) { printf("."); }
            printf("%x", p[i]);
        }

        // netmask
        ifr.ifr_netmask.sa_family = AF_INET;

        if(ioctl(fd, SIOCGIFNETMASK, &ifr) == -1) { break; }

        p = (uint8_t *)&((struct socketaddr_in *)&ifr.ifr_netmask)->sin_addr;
        printf("netmask: ");
        for(int i = 0; i < 4; ++i) {
            if(i) { printf("."); }
            printf("%x", p[i]);
        }

        // broadcast
        ifr.ifr_broadaddr.sa_family = AF_INET;

        if(ioctl(fd, SIOCGIFBRDADDR, &ifr) == -1) { break; }

        p = (uint8_t *)&((struct socketaddr_in *)&ifr.ifr_broadaddr)->sin_addr;
        printf("broadcast: ");
        for(int i = 0; i < 4; ++i) {
            if(i) { printf("."); }
            printf("%x", p[i]);
        }
    } while(0);

    close(fd);
}

static void disp_al() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd == -1) { exit(-1); }
    struct ifreq ifr = {.ifr_ifindex = 0};

    while(1) {
        if(ioctl(fd, SIOCGIFNAME, &ifr) == -1) { break; }
        disp(ifr.ifr_name);
        ++ifr.ifr_ifindex;
    }

    close(fd);
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

static void usage() {
    /*usage表示*/
    fprintf(stderr, "Usage: %s interface [command|address]\n", argv[0]);
    fprintf(stderr, "        -command: up | down\n");
    fprintf(stderr, "        -address: ADDRESS/PREFIX | ADDRESS "
                    "netmask NETMASK\n");
    fprintf(stderr, "%s [-a]\n", argv[0]);
    exit(-1);
}

extern "C" void main(int argc, char **argv) {
    if(argc == 1) {
        disp_al();
        exit(0);
    } else if(argc == 2) {
        if(strcmp(argv[1], "-a") == 0) {
            disp_al();
        } else {
            disp(argv[1]);
        }
        exit(0);
    } else if(argc == 3) {
        if(strcmp(argv[2], "up") == 0) {
            ifup(argv[1]);
            exit(0);
        } else if(strcmp(argv[2], "down") == 0) {
            ifdown(argv[1]);
            exit(0);
        } else {
            s = strchr(argv[2], '/');
            if(!s) { usage(); }
            *s++ = 0;
            if(ip_addr_pton(argv[2], &addr) == -1) { usage(); }
            prefi = atoi(s);
            if(prefi < 0 || prefi > 32) { usage(); }

            netmask = hton32(0xffffffff << (32 - prefi));

            ifset(argv[1], &addr, &netmask);
            exit(0);
        }
    } else if(argc == 5) {
        if(ip_addr_pton(argv[2], &addr) == -1 ||
           strcmp(argv[3], "netmask") != 0 ||
           ip_addr_pton(argv[4], &netmask) == -1) {
            usage();
        }
        ifset(argv[1], &addr, &netmask);
        exit(0);
    } else {
        usage();
    }
}