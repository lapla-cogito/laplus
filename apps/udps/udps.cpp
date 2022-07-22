#include "../socket.h"
#include "../syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern "C" int main(int argc, char *argv[]) {
    int soc, par, ret;
    struct socketaddr_in self, peer;
    unsigned char *addr;
    char buf[2048];

    soc = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(!(soc - 1)) {
        printf("socket failure");
        exit(-1);
    }

    printf("socket success: soc=%d\n", soc);

    self.sin_family = AF_INET;
    self.sin_addr = INADDR_ANY;
    self.sin_port = hton16(7); /*UDPのechoの識別ポート番号は7*/

    if(bind(soc, (struct socketaddr *)&self, sizeof(self)) == -1) {
        printf("bind failure\n");
        close(soc);
        exit(-1);
    }

    addr = (unsigned char *)&self.sin_addr;

    printf("bind success: self=%d.%d.%d.%d:%d\n", addr[0], addr[1], addr[2],
           addr[3], ntoh(self.sin_port));

    while(1) {
        peern = sizeof(peer);
        ret = recvfrom(soc, buf, sizeof(buf), (struct sockaddr *)&peer, &peern);
        if(ret <= 0) {
            printf("<EOF>\n");
            break;
        }

        addr = (unsigned char *)&peer.sin_addr;
        printf("recvfrom success: %d bytes data received, peer=%d.%d.%d.%d:%d",
               ret, addr[0], addr[1], addr[2], addr[3], ntoh(peer.sin_port));

        sendto(soc, buf, ret, (struct sockaddr *)&peer, peern);
    }

    close(soc);
    exit(-1);
}