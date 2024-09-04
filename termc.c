#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <fcntl.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

//
// Hi, if you're here to read the source code, it's probably best to
// go through file-scope procedures, functions, variables, stucts, etc.
// from bottom to top.
// We use this ordering because C code can only depend on definitions
// that come earlier in the file.
//

//
// == Logging and Error Utilities ==
//

static void log_unexpected_error(char* msg) {
    write(STDERR_FILENO, "(termc) UNEXPECTED ERROR: ", 26);
    write(STDERR_FILENO, msg, strlen(msg));
    write(STDERR_FILENO, "\n", 1);
}

static void log_error(char* msg) {
    write(STDERR_FILENO, "(termc) ERROR: ", 15);
    write(STDERR_FILENO, msg, strlen(msg));
    write(STDERR_FILENO, "\n", 1);
}

static void log_warn(char* msg) {
    write(STDERR_FILENO, "(termc) WARNING: ", 17);
    write(STDERR_FILENO, msg, strlen(msg));
    write(STDERR_FILENO, "\n", 1);
}

static void log_errno_and_description(void) {
    char errno_str[4] = {
        (errno/100)%10 + '0', (errno/10)%10 + '0', errno%10 + '0', ' ' };
    write(STDERR_FILENO, "errno=", 6);
    write(STDERR_FILENO, errno_str, 4);
    char* errno_description = strerror(errno);
    write(STDERR_FILENO, errno_description, strlen(errno_description));
    write(STDERR_FILENO, "\n", 1);
}

static void log_unexpected_error_with_errno(char* msg) {
    if (msg != NULL) {
        log_unexpected_error(msg);
        write(STDERR_FILENO, "(termc) ", 8); // Prefix start of newline
    } else {
        write(STDERR_FILENO, "(termc) UNEXPECTED ERROR: ", 26);
    }

    log_errno_and_description();
}

static void log_error_with_errno(char* msg) {
    if (msg != NULL) {
        log_error(msg);
        write(STDERR_FILENO, "(termc) ", 8); // Prefix start of newline
    } else {
        write(STDERR_FILENO, "(termc) ERROR: ", 15);
    }

    log_errno_and_description();
}

//static void log_warn_with_errno(char* msg) {
//    if (msg != NULL) {
//        log_error(msg);
//        write(STDERR_FILENO, "(termc) ", 8); // Prefix start of newline
//    } else {
//        write(STDERR_FILENO, "(termc) WARNING: ", 17);
//    }
//
//    log_errno_and_description();
//}

//static void unexpected_panic(char* err_msg) {
//    if (err_msg != NULL) {
//        log_unexpected_error(err_msg);
//    } else {
//        write(STDERR_FILENO, "(termc) UNEXPECTED ERROR!\n", 25);
//    }
//    exit(EXIT_FAILURE);
//}

static void unexpected_panic_with_errno(char* err_msg) {
    log_unexpected_error_with_errno(err_msg);
    exit(EXIT_FAILURE);
}

//static void panic(char* err_msg) {
//    if (err_msg != NULL) {
//        log_error(err_msg);
//    } else {
//        unexpected_panic(NULL);
//    }
//    exit(EXIT_FAILURE);
//}

static void panic_with_errno(char* err_msg) {
    log_error_with_errno(err_msg);
    exit(EXIT_FAILURE);
}


//
// == Terminal Configuration Utilities ==
//

typedef struct termios TermIOS;

void term_get_termios(TermIOS* termios_struct) {
    if (tcgetattr(STDIN_FILENO, termios_struct) == -1) {
        unexpected_panic_with_errno("tcgetattr(STDIN_FILENO, ...)");
    }
}

void term_set_termios(TermIOS* termios_expected) {
    // We need to loop until all values in the termios struct are set to their
    // expected values because tcsetattr may not apply all changes, if
    // `man termios(3)` is to believed:
    //
    //    Note  that tcsetattr() returns success if any of the requested changes
    //    could be successfully carried out. Therefore, when making multiple
    //    changes it may be necessary to  follow  this  call  with  a further
    //    call to tcgetattr() to check that all changes have been performed
    //    successfully.
    //
    TermIOS termios_actual;
    const size_t MAX_ITERS = 32;
    for (size_t i = 0; i < MAX_ITERS; ++i) {
        tcsetattr(STDIN_FILENO, TCSANOW, termios_expected);

        term_get_termios(&termios_actual);
        if (
            termios_actual.c_iflag == termios_expected->c_iflag &&
            termios_actual.c_oflag == termios_expected->c_oflag &&
            termios_actual.c_cflag == termios_expected->c_cflag &&
            termios_actual.c_lflag == termios_expected->c_lflag &&
            strncmp(
                (const char*) termios_actual.c_cc,
                (const char*) termios_expected->c_cc, NCCS) == 0
        ) {
            return;
        }
    }

    unexpected_panic_with_errno("term_set_termios");
}

// See `man ioctl_tty(2)`
typedef struct term_sz {
    unsigned short rows;
    unsigned short cols;
    unsigned short pixels_x;
    unsigned short pixels_y;
} TermSz;

void term_get_sz(TermSz* term_sz_ptr) {
    const size_t DEFAULT_TERM_COLS = 80;
    const size_t DEFAULT_TERM_ROWS = 24;

    if (ioctl(STDIN_FILENO, TIOCGWINSZ, term_sz_ptr) == -1) {
        term_sz_ptr->cols = DEFAULT_TERM_COLS;
        term_sz_ptr->rows = DEFAULT_TERM_ROWS;

        switch (errno) {
            case EBADF:
            case EFAULT:
                unexpected_panic_with_errno("get_term_sz");
                break;
            case EINVAL:
                log_warn(
                    "Failed to get terminal window size: "
                    "Unsupported ioctl \"TIOCGWINSZ\"!");
                break;
            case ENOTTY:
            default:
                log_unexpected_error_with_errno("get_term_sz");
                break;
        }
    }
}

void term_set_sz(TermSz* term_sz_ptr) {
    if (ioctl(STDIN_FILENO, TIOCSWINSZ, term_sz_ptr) == -1) {
        switch (errno) {
            case EBADF:
            case EFAULT:
                unexpected_panic_with_errno("set_term_sz");
                break;
            case EINVAL:
                log_warn(
                    "Failed to set terminal window size: "
                    "Unsupporte&d ioctl \"TIOCSWINSZ\"!");
                break;
            case ENOTTY:
            default:
                log_unexpected_error_with_errno("set_term_sz");
                break;
        }
    }
}

void term_set_raw(void) {
    TermIOS raw_termios;
    term_get_termios(&raw_termios);

    // TODO: Use cfmakeraw if supported
    raw_termios.c_iflag &=
        ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw_termios.c_oflag &= ~OPOST;
    raw_termios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw_termios.c_cflag &= ~(CSIZE | PARENB);
    raw_termios.c_cflag |= CS8;

    term_set_termios(&raw_termios);
}


//
// == Implementation ==
//

typedef struct managed_fd {
    enum {
        FILE_TYPE_STDIN,
        FILE_TYPE_STDOUT,
        FILE_TYPE_PTY_MASTER,
        FILE_TYPE_SOCK_LISTENER,
        FILE_TYPE_SOCK_CONN,
    } type;
    int fd_when_active;
    struct pollfd* pollfd;
    uint16_t term_out_circular_buf_read_idx;
} ManagedFD;

typedef struct server_state {
    size_t nfds;
    struct pollfd* pollfds;
    ManagedFD* managed_fds;

    size_t    term_out_circular_buf_sz;
    char*     term_out_circular_buf;
    uint16_t* term_out_circular_buf_n_readers_at_idx;
    size_t    term_out_circular_buf_write_idx;
} ServerState;

typedef struct state {
    int     pty_master_fd;
    int     child_shell_connected_to_pty_slave_pid;
    TermIOS parent_shell_termios;
    TermSz  parent_shell_term_sz;

    int requested_exit_status;

    ServerState* server_state;
} State;

State state;

void init_state(
    State* state
) {
    term_get_termios(&state->parent_shell_termios);
    term_get_sz     (&state->parent_shell_term_sz);

    state->requested_exit_status = -1;

    state->server_state = NULL;
}

int do_forkpty(
    State* state
) {
    int child_pid = forkpty(&state->pty_master_fd, NULL, NULL, NULL);
    if (child_pid == -1) {
        unexpected_panic_with_errno("forkpty");
    }
    state->child_shell_connected_to_pty_slave_pid = child_pid;

    return child_pid;
}

void exec_child_shell(
    State* state
) {
    term_set_termios(&state->parent_shell_termios);
    term_set_sz     (&state->parent_shell_term_sz);

    execlp("bash", "bash");
}

void handle_child_shell_sigchld(int sig_num) {
    int child_pid = state.child_shell_connected_to_pty_slave_pid;

    int wait_status;
    waitpid(child_pid, &wait_status, WNOHANG);

    if (WIFSTOPPED(wait_status)) {
        return;
    }

    if (WIFEXITED(wait_status)) {
        state.requested_exit_status = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        // TODO: Better exit status
        state.requested_exit_status = 1;
    }
}

void handle_child_signals(void) {
    struct sigaction sigaction_struct = {0};
    sigaction_struct.sa_handler = handle_child_shell_sigchld;
    if (sigaction(SIGCHLD, &sigaction_struct, NULL) == -1) {
        unexpected_panic_with_errno("sigaction");
    }
}

void server__init_state(
    State* state
) {
    ServerState* server_state = state->server_state;

    server_state->term_out_circular_buf_sz = 64*1024;
    server_state->term_out_circular_buf =
        calloc(server_state->term_out_circular_buf_sz, sizeof(char));
    server_state->term_out_circular_buf_n_readers_at_idx =
        calloc(server_state->term_out_circular_buf_sz, sizeof(uint16_t));
    server_state->term_out_circular_buf_write_idx = 0;

    server_state->nfds = 64;
    server_state->pollfds     = calloc(server_state->nfds, sizeof(struct pollfd));
    server_state->managed_fds = calloc(server_state->nfds, sizeof(ManagedFD));

    for (size_t i = 0; i < server_state->nfds; ++i) {
        ManagedFD* managed_fd = &server_state->managed_fds[i];
        struct pollfd* pollfd = &server_state->pollfds[i];

        managed_fd->type                           = FILE_TYPE_SOCK_CONN;
        managed_fd->fd_when_active                 = i;
        managed_fd->pollfd                         = pollfd;
        managed_fd->term_out_circular_buf_read_idx = 0;

        pollfd->fd      = -1;
        pollfd->events  =  0;
        pollfd->revents =  0;
    }

    server_state->managed_fds[STDIN_FILENO].type   = FILE_TYPE_STDIN;
    server_state->pollfds    [STDIN_FILENO].fd     = STDIN_FILENO;
    server_state->pollfds    [STDIN_FILENO].events = POLLIN;

    server_state->managed_fds[STDOUT_FILENO].type   = FILE_TYPE_STDOUT;
    server_state->pollfds    [STDOUT_FILENO].fd     = STDOUT_FILENO;
    ++server_state->term_out_circular_buf_n_readers_at_idx[STDOUT_FILENO];

    server_state->managed_fds[state->pty_master_fd].type   = FILE_TYPE_PTY_MASTER;
    server_state->pollfds    [state->pty_master_fd].fd     = state->pty_master_fd;
    server_state->pollfds    [state->pty_master_fd].events = POLLIN;
}

void server__start(
    State* state
) {
    ServerState* server_state = state->server_state;

    struct addrinfo addrinfo_hints;
    memset(&addrinfo_hints, 0, sizeof(addrinfo_hints));
    addrinfo_hints.ai_family   = AF_UNSPEC;
    addrinfo_hints.ai_socktype = SOCK_STREAM;
    addrinfo_hints.ai_flags    = AI_PASSIVE;
    struct addrinfo* addrinfo_result;
    if (getaddrinfo(
        NULL, "8080",
        &addrinfo_hints, &addrinfo_result) != 0
    )
        panic_with_errno("getaddrinfo");


    int listen_sock_fd = -1;
    bool bound_sock = false;
    for (struct addrinfo* p = addrinfo_result;
         p != NULL;
         p = p->ai_next
    ) {
        listen_sock_fd = socket(p->ai_family,
                                p->ai_socktype,
                                p->ai_protocol);
        if (listen_sock_fd == -1)
            continue;

        server_state->managed_fds[listen_sock_fd].type   = listen_sock_fd;
        server_state->pollfds    [listen_sock_fd].fd     = listen_sock_fd;
        server_state->pollfds    [listen_sock_fd].events = POLLIN;

        if (bind(listen_sock_fd,
                 p->ai_addr,
                 p->ai_addrlen) == -1
        ) {
            close(listen_sock_fd);
            continue;
        }

        bound_sock = true;
        break;
    }
    freeaddrinfo(addrinfo_result);
    if (!bound_sock)
        panic_with_errno("Failed to bind to IPv4 or IPv6 address");

    int yes = 1;
    setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    fcntl(listen_sock_fd, F_SETFD, O_NONBLOCK);

    // Start listening for connections
    if (listen(listen_sock_fd, 10) == -1)
        panic_with_errno("listen");
}

int server__term_out__get_max_writable_bytes(
    State* state
) {
    ServerState* server_state = state->server_state;
    size_t    idx            = server_state->term_out_circular_buf_write_idx;
    size_t    circ_buf_sz    = server_state->term_out_circular_buf_sz;
    uint16_t* readers_at_idx = server_state->term_out_circular_buf_n_readers_at_idx;

    int n = 0;
    for (size_t i = 0; i < circ_buf_sz; ++i) {
        if (readers_at_idx[i] > 0)
            break;

        idx = (idx + 1) % circ_buf_sz;
        ++n;
    }

    return n;
}

void server__term_out__write(
    State* state,
    char* buf, size_t n
) {
    ServerState* server_state = state->server_state;
    size_t* write_idx   = &server_state->term_out_circular_buf_write_idx;
    char*   circ_buf    =  server_state->term_out_circular_buf;
    size_t  circ_buf_sz =  server_state->term_out_circular_buf_sz;

    for (size_t i = 0; i < n; ++i) {
        circ_buf[*write_idx] = buf[i];

        *write_idx = (*write_idx + 1) % circ_buf_sz;
    }
}

typedef enum copy_to_fd_mode {
    USE_WRITE,
    USE_SEND,
} CopyToFdMode;

int server__term_out__copy_to_fd(
    State* state,
    int fd, CopyToFdMode mode
) {
    ServerState* server_state = state->server_state;

    if (fd < 0 || fd > server_state->nfds) {
        log_unexpected_error_with_errno(
            "server__term_out__copy_to_fd: fd out of range");
        return -1;
    }

    size_t write_idx   = server_state->term_out_circular_buf_write_idx;
    char*  circ_buf    = server_state->term_out_circular_buf;
    size_t circ_buf_sz = server_state->term_out_circular_buf_sz;

    ManagedFD* managed_fd = &server_state->managed_fds[fd];
    uint16_t* read_idx = &managed_fd->term_out_circular_buf_read_idx;

    int n = 0;
    if (*read_idx < write_idx) {
        switch (mode) {
            case USE_WRITE:
                n = write(fd, circ_buf + *read_idx, write_idx - *read_idx);
                if (n == -1)
                    return -1;
                break;
            case USE_SEND:
                n = send(fd, circ_buf + *read_idx, write_idx - *read_idx, 0);
                if (n == -1)
                    return -1;
                break;
            default:
                log_unexpected_error_with_errno(
                    "server__term_out__copy_to_fd: Invalid mode");
                return -1;
        }
    } else if (*read_idx > write_idx) {
        int n_partial;
        switch (mode) {
            case USE_WRITE:
                n_partial =
                    write(fd, circ_buf + *read_idx, circ_buf_sz - *read_idx);
                if (n_partial == -1)
                    return -1;
                n += n_partial;

                n_partial = write(fd, circ_buf, write_idx);
                if (n_partial == -1) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                        return -1;
                } else {
                    n += n_partial;
                }
                break;
            case USE_SEND:
                n_partial =
                    send(fd, circ_buf + *read_idx, circ_buf_sz - *read_idx, 0);
                if (n_partial == -1)
                    return -1;
                n += n_partial;

                n_partial = send(fd, circ_buf, write_idx, 0);
                if (n_partial == -1) {
                    if (errno != EWOULDBLOCK && errno != EAGAIN)
                        return -1;
                } else {
                    n += n_partial;
                }
                break;
            default:
                log_unexpected_error_with_errno(
                    "server__term_out__copy_to_fd: Invalid mode");
                return -1;
        }
    } else {
        return 0;
    }

    uint16_t* n_readers_at_idx = server_state->term_out_circular_buf_n_readers_at_idx;
    if (n_readers_at_idx[*read_idx] > 0)
        --n_readers_at_idx[*read_idx];
    *read_idx = (*read_idx + n) % circ_buf_sz;
    ++n_readers_at_idx[*read_idx];

    return n;
}

void server__run_main_loop(
    State* state
) {
    ServerState* server_state = state->server_state;

    const size_t  read_buf_sz = 1024;
    char read_buf[read_buf_sz];

    //int test_fd = creat("test.txt", 0640);

    while (state->requested_exit_status == -1) {
        if (poll(server_state->pollfds, server_state->nfds, -1) == -1) {
            switch (errno) {
                // TODO
            }
            unexpected_panic_with_errno("poll");
        }

        for (size_t fd = 0; fd < server_state->nfds; ++fd) {
            struct pollfd* pollfd = &server_state->pollfds[fd];
            if (pollfd->fd < 0 || pollfd->revents == 0)
                continue;

            ManagedFD* managed_fd = &server_state->managed_fds[fd];

            switch (managed_fd->type) {
                case FILE_TYPE_STDIN: {
                    size_t n = read(STDIN_FILENO, read_buf, read_buf_sz);
                    if (n == -1) {
                        // TODO
                        unexpected_panic_with_errno("read");
                    }
                    if (write(state->pty_master_fd, read_buf, n) != n) {
                        // TODO
                        unexpected_panic_with_errno("write");
                    }
                }; break;

                case FILE_TYPE_PTY_MASTER: {
                    size_t max_n = server__term_out__get_max_writable_bytes(state);
                    if (max_n == 0) {
                        server_state->pollfds[STDOUT_FILENO].events = 0;
                        continue;
                    } else if (max_n > read_buf_sz) {
                        max_n = read_buf_sz;
                    }

                    size_t n = read(state->pty_master_fd, read_buf, max_n);
                    if (n == 0) {
                        server_state->pollfds[STDOUT_FILENO].events = 0;
                        continue;
                    } else if (n == -1) {
                        bool master_pty_closed_by_child_terminating =
                            state->requested_exit_status != -1 &&
                            errno == EIO;
                        if (!master_pty_closed_by_child_terminating) {
                            // TODO
                            unexpected_panic_with_errno("read");
                        }
                    } else {
                        server__term_out__write(state, read_buf, n);
                        server_state->pollfds[STDOUT_FILENO].events = POLLOUT;
                    }
                }; break;

                case FILE_TYPE_STDOUT: {
                    int n = server__term_out__copy_to_fd(state, fd, USE_WRITE);
                    if (n == 0) {
                        server_state->pollfds[STDOUT_FILENO].events = 0;
                    } else if (n == -1) {
                        log_unexpected_error_with_errno("server__term_out__copy_to_fd");
                    }
                    server_state->pollfds[STDOUT_FILENO].events = 0;
                }; break;

                case FILE_TYPE_SOCK_LISTENER: {
                    struct sockaddr_storage conn_addr;
                    socklen_t               conn_addr_sz = sizeof(conn_addr_sz);
                    int conn_fd = accept(fd,
                                         (struct sockaddr*) &conn_addr,
                                         &conn_addr_sz);
                    fcntl(conn_fd, F_SETFD, O_NONBLOCK);
                    if (conn_fd == -1) {
                        perror("accept");
                        continue;
                    }
                    server_state->pollfds[conn_fd].fd = conn_fd;
                }; break;

                case FILE_TYPE_SOCK_CONN:
                    break;
            }

            //if (managed_fd->type == FILE_TYPE_SOCK_LISTENER) {
            //    struct sockaddr_storage conn_addr;
            //    socklen_t               conn_addr_sz = sizeof(conn_addr_sz);
            //    int conn_fd = accept(pollfd->fd,
            //                         (struct sockaddr*) &conn_addr,
            //                         &conn_addr_sz);
            //    fcntl(conn_fd, F_SETFD, O_NONBLOCK);
            //    if (conn_fd == -1) {
            //        perror("accept");
            //        continue;
            //    }
            //    server_state->pollfds[conn_fd].fd = conn_fd;

            //    printf("Guest joined.\n\r");
            //} else if (pollfd->fd == STDIN_FILENO) {
            //    size_t n = read(STDIN_FILENO, read_buf, read_buf_sz);
            //    if (n == -1) {
            //        // TODO
            //        unexpected_panic_with_errno("read");
            //    }
            //    if (write(state->pty_master_fd, read_buf, n) != n) {
            //        // TODO
            //        unexpected_panic_with_errno("write");
            //    }
            //} else if (pollfd->fd == state->pty_master_fd) {
            //    size_t n = read(state->pty_master_fd, read_buf, read_buf_sz);
            //    if (n == -1) {
            //        bool master_pty_closed_by_child_terminating =
            //            state->requested_exit_status != -1 &&
            //            errno == EIO;
            //        if (!master_pty_closed_by_child_terminating) {
            //            // TODO
            //            unexpected_panic_with_errno("read");
            //        }
            //    }
            //    if (write(STDOUT_FILENO, read_buf, n) != n) {
            //        // TODO
            //        unexpected_panic_with_errno("write");
            //    }
            //    if (write(test_fd, read_buf, n) != n) {
            //        // TODO
            //        unexpected_panic_with_errno("write");
            //    }
            //}
        }
    }
}

void cleanup(
    State* state
) {
    term_set_termios(&state->parent_shell_termios);

    ServerState* server_state = state->server_state;
    if (server_state != NULL) {
        for (size_t i = 0; i < server_state->nfds; ++i) {
            ManagedFD* managed_fd = &server_state->managed_fds[i];
            close(managed_fd->fd_when_active);
        }

        free(server_state->pollfds);
        free(server_state->managed_fds);
    }

    close(state->pty_master_fd);
}

int main(void) {
    state = (State) {0};

    init_state(&state);

    term_set_raw();

    int child_pid = do_forkpty(&state);
    bool is_child_proc_with_pty_slave_controlling_term = child_pid == 0;
    bool is_parent_proc = !is_child_proc_with_pty_slave_controlling_term;

    if (is_child_proc_with_pty_slave_controlling_term)
        exec_child_shell(&state);

    if (is_parent_proc) {
        handle_child_signals();

        // Run the server
        {
            ServerState server_state;
            state.server_state = &server_state;

            server__init_state(&state);

            server__start(&state);

            server__run_main_loop(&state);
        }
    }

    cleanup(&state);

    exit(state.requested_exit_status);
}

