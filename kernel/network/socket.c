#include "socket.h"
#include "benri.h"
#include "if.h"
#include "ip.h"
#include "network.h"
#include "tcp.h"
#include "udp.h"
#include <string.h>

static struct socket sockets[128];

static struct socket *socketalloc(void) {
    struct socket *entry;

    for(entry = sockets; entry < tailof(sockets); entry++) {
        if(!entry->used) {
            entry->used = 1;
            return entry;
        }
    }
    return NULL;
}

struct socket *socketget(int id) {
    if(id < 0 || id >= (int)countof(sockets)) {
        /* out of range */
        return NULL;
    }
    return &sockets[id];
}

int socketopen(int domain, int type, int protocol) {
    struct socket *s;

    if(domain != AF_INET || (type != SOCK_STREAM && type != SOCK_DGRAM) ||
       protocol != 0) {
        return -1;
    }
    s = socketalloc();
    if(!s) { return -1; }
    s->family = domain;
    s->type = type;
    switch(s->type) {
    case SOCK_STREAM:
        s->desc = tcp_open();
        break;
    case SOCK_DGRAM:
        s->desc = udp_open();
        break;
    }
    if(s->desc == -1) { return -1; }
    return indexof(sockets, s);
}

int socketclose(struct socket *s) {
    switch(s->type) {
    case SOCK_STREAM:
        tcp_close(s->desc);
        break;
    case SOCK_DGRAM:
        udp_close(s->desc);
        break;
    default:
        return -1;
    }
    memset(s, 0, sizeof(*s));
    return 0;
}

int socketioctl(struct socket *s, int req, void *arg) {
    struct ifreq *ifreq;
    struct net_device *dev;
    struct ip_iface *iface;
    char addr[IP_ADDR_STR_LEN];

    ifreq = (struct ifreq *)arg;
    switch((unsigned int)req) {
    case SIOCGIFINDEX:
        dev = net_device_by_name(ifreq->ifr_name);
        if(!dev) { return -1; }
        ifreq->ifr_ifindex = dev->index;
        break;
    case SIOCGIFNAME:
        dev = net_device_by_index(ifreq->ifr_ifindex);
        if(!dev) { return -1; }
        strncpy(ifreq->ifr_name, dev->name, sizeof(ifreq->ifr_name));
        break;
    case SIOCSIFNAME:
        // TODO
        break;
    case SIOCGIFHWADDR:
        dev = net_device_by_name(ifreq->ifr_name);
        if(!dev) { return -1; }
        // TODO: HW type check
        memcpy(ifreq->ifr_hwaddr.sa_data, dev->addr, dev->alen);
        break;
    case SIOCSIFHWADDR:
        // TODO
        break;
    case SIOCGIFFLAGS:
        dev = net_device_by_name(ifreq->ifr_name);
        if(!dev) { return -1; }
        ifreq->ifr_flags = dev->flags;
        break;
    case SIOCSIFFLAGS:
        dev = net_device_by_name(ifreq->ifr_name);
        if(!dev) { return -1; }
        if((dev->flags & IFF_UP) != (ifreq->ifr_flags & IFF_UP)) {
            if(ifreq->ifr_flags & IFF_UP) {
                net_device_open(dev);
            } else {
                net_device_close(dev);
            }
        }
        break;
    case SIOCGIFADDR:
        dev = net_device_by_name(ifreq->ifr_name);
        if(!dev) { return -1; }
        switch(ifreq->ifr_addr.sa_family) {
        case AF_INET:
            iface = (struct ip_iface *)net_device_get_iface(
                dev, NET_IFACE_FAMILY_IP);
            if(!iface) { return -1; }
            ((struct sockaddr_in *)&ifreq->ifr_addr)->sin_addr = iface->unicast;
            break;
        default:
            return -1;
        }
        break;
    case SIOCSIFADDR:
        dev = net_device_by_name(ifreq->ifr_name);
        if(!dev) { return -1; }
        switch(ifreq->ifr_addr.sa_family) {
        case AF_INET:
            iface = (struct ip_iface *)net_device_get_iface(
                dev, NET_IFACE_FAMILY_IP);
            if(iface) {
                if(ip_iface_reconfigure(
                       iface,
                       ((struct sockaddr_in *)&ifreq->ifr_addr)->sin_addr,
                       iface->netmask) == -1) {
                    return -1;
                }
            } else {
                iface = ip_iface_alloc(
                    ip_addr_ntop(
                        ((struct sockaddr_in *)&ifreq->ifr_addr)->sin_addr,
                        addr, sizeof(addr)),
                    "255.255.255.255");
                if(!iface) { return -1; }
                ip_iface_register(dev, iface);
            }
            break;
        default:
            return -1;
        }
        break;
    case SIOCGIFNETMASK:
        dev = net_device_by_name(ifreq->ifr_name);
        if(!dev) { return -1; }
        switch(ifreq->ifr_addr.sa_family) {
        case AF_INET:
            iface = (struct ip_iface *)net_device_get_iface(
                dev, NET_IFACE_FAMILY_IP);
            if(!iface) { return -1; }
            ((struct sockaddr_in *)&ifreq->ifr_netmask)->sin_addr =
                iface->netmask;
            break;
        default:
            return -1;
        }
        break;
    case SIOCSIFNETMASK:
        dev = net_device_by_name(ifreq->ifr_name);
        if(!dev) { return -1; }
        switch(ifreq->ifr_addr.sa_family) {
        case AF_INET:
            iface = (struct ip_iface *)net_device_get_iface(
                dev, NET_IFACE_FAMILY_IP);
            if(!iface) { return -1; }
            if(ip_iface_reconfigure(
                   iface, iface->unicast,
                   ((struct sockaddr_in *)&ifreq->ifr_addr)->sin_addr) == -1) {
                return -1;
            }
            break;
        default:
            return -1;
        }
        break;
    case SIOCGIFBRDADDR:
        dev = net_device_by_name(ifreq->ifr_name);
        if(!dev) { return -1; }
        switch(ifreq->ifr_addr.sa_family) {
        case AF_INET:
            iface = (struct ip_iface *)net_device_get_iface(
                dev, NET_IFACE_FAMILY_IP);
            if(!iface) { return -1; }
            ((struct sockaddr_in *)&ifreq->ifr_broadaddr)->sin_addr =
                iface->broadcast;
            break;
        default:
            return -1;
        }
        break;
    case SIOCSIFBRDADDR:
        // TODO
        break;
    case SIOCGIFMTU:
        dev = net_device_by_name(ifreq->ifr_name);
        if(!dev) { return -1; }
        ifreq->ifr_mtu = dev->mtu;
        break;
    case SIOCSIFMTU:
        break;
    default:
        return -1;
    }
    return 0;
}

int socketrecvfrom(struct socket *s, char *buf, int n, struct sockaddr *addr,
                   int *addrlen) {
    struct udp_endpoint ep;
    int ret;

    if(s->type != SOCK_DGRAM) { return -1; }
    switch(s->family) {
    case AF_INET:
        ret = udp_recvfrom(s->desc, (uint8_t *)buf, n, &ep);
        if(ret != -1) {
            ((struct sockaddr_in *)addr)->sin_addr = ep.addr;
            ((struct sockaddr_in *)addr)->sin_port = ep.port;
        }
        return ret;
    }
    return -1;
}

int socketsendto(struct socket *s, char *buf, int n, struct sockaddr *addr,
                 int addrlen) {
    struct udp_endpoint ep;

    if(s->type != SOCK_DGRAM) { return -1; }
    switch(s->family) {
    case AF_INET:
        ep.addr = ((struct sockaddr_in *)addr)->sin_addr;
        ep.port = ((struct sockaddr_in *)addr)->sin_port;
        return udp_sendto(s->desc, (uint8_t *)buf, n, &ep);
    }
    return -1;
}

int socketbind(struct socket *s, struct sockaddr *addr, int addrlen) {
    struct tcp_endpoint tcp_ep;
    struct udp_endpoint udp_ep;

    switch(s->type) {
    case SOCK_STREAM:
        switch(s->family) {
        case AF_INET:
            tcp_ep.addr = ((struct sockaddr_in *)addr)->sin_addr;
            tcp_ep.port = ((struct sockaddr_in *)addr)->sin_port;
            return tcp_bind(s->desc, &tcp_ep);
        }
        return -1;
    case SOCK_DGRAM:
        switch(s->family) {
        case AF_INET:
            udp_ep.addr = ((struct sockaddr_in *)addr)->sin_addr;
            udp_ep.port = ((struct sockaddr_in *)addr)->sin_port;
            return udp_bind(s->desc, &udp_ep);
        }
        return -1;
    }
    return -1;
}

int socketlisten(struct socket *s, int backlog) {
    if(s->type != SOCK_STREAM) { return -1; }
    switch(s->family) {
    case AF_INET:
        return tcp_listen(s->desc, backlog);
    }
    return -1;
}

int socketaccept(struct socket *s, struct sockaddr *addr, int *addrlen) {
    struct tcp_endpoint ep;
    int ret;
    struct socket *new_s;

    if(s->type != SOCK_STREAM) { return -1; }
    switch(s->family) {
    case AF_INET:
        ret = tcp_accept(s->desc, &ep);
        if(ret == -1) { return -1; }
        ((struct sockaddr_in *)addr)->sin_addr = ep.addr;
        ((struct sockaddr_in *)addr)->sin_port = ep.port;
        new_s = socketalloc();
        new_s->family = s->family;
        new_s->type = s->type;
        new_s->desc = ret;
        return indexof(sockets, new_s);
    }
    return -1;
}

int socketconnect(struct socket *s, struct sockaddr *addr, int addrlen) {
    struct tcp_endpoint tcp_ep;

    if(s->type != SOCK_STREAM) { return -1; }
    switch(s->family) {
    case AF_INET:
        tcp_ep.addr = ((struct sockaddr_in *)addr)->sin_addr;
        tcp_ep.port = ((struct sockaddr_in *)addr)->sin_port;
        return tcp_connect(s->desc, &tcp_ep);
    }
    return -1;
}

int socketrecv(struct socket *s, char *buf, int n) {
    if(s->type != SOCK_STREAM) { return -1; }
    switch(s->family) {
    case AF_INET:
        return tcp_receive(s->desc, (uint8_t *)buf, n);
    }
    return -1;
}

int socketsend(struct socket *s, char *buf, int n) {
    if(s->type != SOCK_STREAM) { return -1; }
    switch(s->family) {
    case AF_INET:
        return tcp_send(s->desc, (uint8_t *)buf, n);
    }
    return -1;
}
