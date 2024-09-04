#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    struct addrinfo  hints;
    struct addrinfo* servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    int status;
    if ((status = getaddrinfo(NULL, "1234", &hints, &servinfo)) != 0) {
        perror("getaddrinfo");
        exit(1);
    }

    freeaddrinfo(servinfo);

    return 0;
}
