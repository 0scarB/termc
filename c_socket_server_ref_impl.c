#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define POLLFDS_N 512

int main(int argc, char* argv[]) {
    struct addrinfo addrinfo_hints;
    memset(&addrinfo_hints, 0, sizeof(addrinfo_hints));

    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        //
        // Fill addrinfo struct for the server's address and bind to
        // that address.
        //
        addrinfo_hints.ai_family   = AF_UNSPEC;
        addrinfo_hints.ai_socktype = SOCK_STREAM;
        addrinfo_hints.ai_flags    = AI_PASSIVE;

        struct addrinfo* addrinfo_result;
        if (getaddrinfo(
            NULL, "8080",
            &addrinfo_hints, &addrinfo_result) != 0
        ) {
            perror("getaddrinfo");
            exit(1);
        }

        int listen_sock_fd = -1;
        bool bound_socket = false;
        for (struct addrinfo* p = addrinfo_result;
             p != NULL;
             p = p->ai_next
        ) {
            listen_sock_fd = socket(p->ai_family,
                                    p->ai_socktype,
                                    p->ai_protocol);
            if (listen_sock_fd == -1)
                continue;

            if (bind(listen_sock_fd,
                     p->ai_addr,
                     p->ai_addrlen) == -1
            ) {
                close(listen_sock_fd);
                continue;
            }

            bound_socket = true;
            break;
        }
        freeaddrinfo(addrinfo_result);

        int yes = 1;
        setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        fcntl(listen_sock_fd, F_SETFD, O_NONBLOCK);

        if (!bound_socket) {
            perror("Failed to bind IPv4 and IPv6");
            exit(1);
        }

        // Start listening for connections
        if (listen(listen_sock_fd, 10) == -1) {
            perror("listen");
            exit(1);
        }

        struct pollfd pollfds[POLLFDS_N];
        for (size_t i = 0; i < POLLFDS_N; ++i) {
            pollfds[i] = (struct pollfd) {
                .fd     = -1,
                .events = POLLIN,
            };
        }
        pollfds[listen_sock_fd].fd = listen_sock_fd;

        char   conn_recv_bufs[POLLFDS_N][1024] = {0};
        size_t conn_recv_lens[POLLFDS_N]       = {0};
        size_t conn_count = 0;

        while (true) {
            // Poll sockets for I/O events
            if (poll(pollfds, POLLFDS_N, -1) == -1) {
                perror("poll");
                goto server_cleanup;
            }

            for (int i = 0; i < POLLFDS_N; ++i) {
                struct pollfd* pollfd = &pollfds[i];
                if (pollfd->fd < 0 || pollfd->revents == 0)
                    continue;

                if (pollfd->fd == listen_sock_fd) {
                    //
                    // If the listening socket is ready to read from,
                    // accept the new connection socket and add it to the
                    // list of sockets to poll from.
                    //
                    struct sockaddr_storage conn_addr;
                    socklen_t               conn_addr_sz = sizeof(conn_addr_sz);
                    int conn_fd = accept(listen_sock_fd,
                                         (struct sockaddr*) &conn_addr,
                                         &conn_addr_sz);
                    fcntl(conn_fd, F_SETFD, O_NONBLOCK);
                    if (conn_fd == -1) {
                        perror("accept");
                        continue;
                    }
                    pollfds[conn_fd].fd = conn_fd;
                    ++conn_count;

                    printf("Client joined. "
                           "Active connections = %ld.\n",
                           conn_count);
                } else {
                    //
                    // Handle connection sockets.
                    //
                    char*   recv_buf = conn_recv_bufs[i];
                    size_t* recv_len = conn_recv_lens+i;

                    if (pollfd->revents & POLLIN) {
                        //
                        // If the socket has input, store the input in recv_buf
                        // and start polling for when the socket is ready for
                        // output.
                        //
                        if (*recv_len >= 1023)
                            continue;

                        int n = recv(pollfd->fd,
                                     recv_buf+(*recv_len),
                                     1023-(*recv_len), 0);
                        if (n == 0) {
                            // Remove the connection.
                            close(pollfd->fd);
                            pollfd->fd = -1;
                            --conn_count;

                            printf("Client left. "
                                   "Remaining active connections = %ld.\n",
                                   conn_count);
                            continue;
                        } else if (n == -1) {
                            perror("connection recv");
                            continue;
                        }
                        *recv_len += n;
                        recv_buf[*recv_len] = '\0';

                        pollfd->events |= POLLOUT;
                    } else if (pollfd->revents & POLLOUT) {
                        //
                        // If the socket is ready for output, interpret recv_buf
                        // as a command and send the command's output on the
                        // socket.
                        //
                        if (recv_buf[0] == '\0')
                            continue;

                        char send_buf[1024] = {0};
                        char send_len = 0;

                        if (strncmp(recv_buf, "ping", *recv_len) == 0) {
                            send_len = sprintf(send_buf, "pong");
                        } else if (strncmp(recv_buf, "exit", *recv_len) == 0) {
                            goto server_cleanup;
                        } else if (*recv_len >= 1023) {
                            send_len = sprintf(send_buf, "Command too long!");
                        } else {
                            send_len = sprintf(send_buf, "Invalid command!");
                        }

                        if (send(pollfd->fd, send_buf, send_len, 0) == -1) {
                            perror("send");
                            continue;
                        }

                        memset(recv_buf, '\0', *recv_len);
                        *recv_len = 0;

                        // Stop polling for output to be ready.
                        pollfd->events &= ~POLLOUT;
                    }
                }
            }
        }
        server_cleanup:
            for (size_t i = 0; i < POLLFDS_N; ++i) {
                shutdown(pollfds[i].fd, O_RDWR);
                close(pollfds[i].fd);
            } printf("Exited.\n");
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        memset(&addrinfo_hints, 0, sizeof(addrinfo_hints));
        addrinfo_hints.ai_family   = AF_UNSPEC;
        addrinfo_hints.ai_socktype = SOCK_STREAM;

        struct addrinfo* addrinfo_result;
        if (getaddrinfo(
            "127.0.0.1", "8080",
            &addrinfo_hints, &addrinfo_result) != 0
        ) {
            perror("getaddrinfo");
            exit(1);
        }

        int conn_sock_fd = -1;
        bool connected_socket = false;
        for (struct addrinfo* p = addrinfo_result;
             p != NULL;
             p = p->ai_next
        ) {
            conn_sock_fd = socket(p->ai_family,
                                  p->ai_socktype,
                                  p->ai_protocol);
            if (conn_sock_fd == -1)
                continue;

            if (connect(conn_sock_fd,
                        p->ai_addr,
                        p->ai_addrlen) == -1
            ) {
                close(conn_sock_fd);
                continue;
            }

            connected_socket = true;
            break;
        }
        freeaddrinfo(addrinfo_result);
        if (!connected_socket) {
            perror("Failed to connect to IPv4 and IPv6");
            exit(1);
        }

        printf("Commands:\n"
               "    ping: Server responds with 'pong'.\n"
               "    exit: Shutdown the server.\n");
        while (true) {
            char cmd[1024];
            int n = read(STDIN_FILENO, cmd, 1024);
            if (n == -1) {
                perror("read");
                goto client_cleanup;
            }
            cmd[n - 1] = '\0';

            if (send(conn_sock_fd, cmd, strlen(cmd), 0) == -1) {
                perror("send");
                goto client_cleanup;
            }

            char recv_buf[1024];
            int recv_len = recv(conn_sock_fd, recv_buf, 1024, 0);
            recv_buf[recv_len] = '\0';
            if (n == 0) {
                break;
            } else if (n != -1) {
                printf("%s\n", recv_buf);
            } else {
                perror("recv");
                goto client_cleanup;
            }
        }

        client_cleanup:
            close(conn_sock_fd);
    } else {
        fprintf(stderr,
                "First argument must be 'server' or 'client'\n");
        exit(1);
    }

    exit(0);
}

