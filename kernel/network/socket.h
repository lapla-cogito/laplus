#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ip.h"
#include "sockio.h"
#include <stdint.h>

#define PF_UNSPEC 0
#define PF_LOCAL 1
#define PF_INET 2
#define PF_INET6 10

#define AF_UNSPEC PF_UNSPEC
#define AF_LOCAL PF_LOCAL
#define AF_INET PF_INET
#define AF_INET6 PF_INET6

#define SOCK_STREAM 1
#define SOCK_DGRAM 2

#define IPPROTO_TCP 0
#define IPPROTO_UDP 0

#define INADDR_ANY ((ip_addr_t)0)

struct socket {
    int used;
    int family;
    int type;
    int desc;
};

struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    unsigned short sin_family;
    uint16_t sin_port;
    ip_addr_t sin_addr;
};

#define IFNAMSIZ 16

struct ifreq {
    char ifr_name[IFNAMSIZ]; /* Interface name */
    union {
        struct sockaddr ifr_addr;
        struct sockaddr ifr_dstaddr;
        struct sockaddr ifr_broadaddr;
        struct sockaddr ifr_netmask;
        struct sockaddr ifr_hwaddr;
        short ifr_flags;
        int ifr_ifindex;
        int ifr_metric;
        int ifr_mtu;
        char ifr_slave[IFNAMSIZ];
        char ifr_newname[IFNAMSIZ];
        char *ifr_data;
    };
};

extern int socketopen(int domain, int type, int protocol);
struct socket *socketget(int index);
extern int socketclose(struct socket *s);
extern int socketioctl(struct socket *s, int req, void *arg);
extern int socketrecvfrom(struct socket *s, char *buf, int n,
                          struct sockaddr *addr, int *addrlen);
extern int socketsendto(struct socket *s, char *buf, int n,
                        struct sockaddr *addr, int addrlen);
extern int socketbind(struct socket *s, struct sockaddr *addr, int addrlen);
extern int socketlisten(struct socket *, int);
extern int socketaccept(struct socket *, struct sockaddr *, int *);
extern int socketconnect(struct socket *, struct sockaddr *, int);
extern int socketrecv(struct socket *, char *, int);
extern int socketsend(struct socket *, char *, int);

#ifdef __cplusplus
}
#endif
