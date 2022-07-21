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
}