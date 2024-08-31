#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static void log_unexpected_error(char* msg) {
    write(STDERR_FILENO, "(termc) UNEXPECTED ERROR: ", 26);
    write(STDERR_FILENO, msg, strlen(msg));
    write(STDERR_FILENO, "\n", 1);
}

//static void log_error(char* msg) {
//    write(STDERR_FILENO, "(termc) ERROR: ", 15);
//    write(STDERR_FILENO, msg, strlen(msg));
//    write(STDERR_FILENO, "\n", 1);
//}

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

//static void log_error_with_errno(char* msg) {
//    if (msg != NULL) {
//        log_error(msg);
//        write(STDERR_FILENO, "(termc) ", 8); // Prefix start of newline
//    } else {
//        write(STDERR_FILENO, "(termc) ERROR: ", 15);
//    }
//
//    log_errno_and_description();
//}

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

//static void panic_with_errno(char* err_msg) {
//    log_error_with_errno(err_msg);
//    exit(EXIT_FAILURE);
//}

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

int child_pid;
int exit_status = -1;

void handle_sigchld(int sig_num) {
    int wait_status;
    waitpid(child_pid, &wait_status, WNOHANG);

    if (WIFSTOPPED(wait_status)) {
        return;
    }

    if (WIFEXITED(wait_status)) {
        exit_status = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        // TODO: Better exit status
        exit_status = 1;
    }
}

int main(void) {
    TermIOS orig_termios;
    TermSz  orig_term_sz;
    term_get_termios(&orig_termios);
    term_get_sz     (&orig_term_sz);

    int master_pty_fd;
    child_pid = forkpty(&master_pty_fd, NULL, NULL, NULL);
    if (child_pid == -1) {
        unexpected_panic_with_errno("forkpty");
    }
    bool is_child_proc  = child_pid == 0;
    bool is_parent_proc = !is_child_proc;

    if (is_child_proc) {
        term_set_termios(&orig_termios);
        term_set_sz     (&orig_term_sz);

        execlp("bash", "bash");
    }

    if (is_parent_proc) {
        struct pollfd poll_structs[] = {
            {
                STDIN_FILENO,
                POLLIN,
                0,
            },
            {
                master_pty_fd,
                POLLIN,
                0,
            },
        };
        int poll_structs_n = sizeof(poll_structs)/sizeof(struct pollfd);

        const size_t  read_buf_sz = 1024;
        char read_buf[read_buf_sz];

        struct sigaction sigaction_struct = {0};
        sigaction_struct.sa_handler = handle_sigchld;
        if (sigaction(SIGCHLD, &sigaction_struct, NULL) == -1) {
            unexpected_panic_with_errno("sigaction");
        }

        int test_fd = creat("test.txt", 0640);

        term_set_raw();

        while (exit_status == -1) {
            if (poll(poll_structs, poll_structs_n, -1) == -1) {
                switch (errno) {
                    // TODO
                }
                unexpected_panic_with_errno("poll");
            }

            for (size_t i = 0; i < poll_structs_n; ++i) {
                struct pollfd poll_struct = poll_structs[i];

                if (poll_struct.revents == 0) continue;

                if (poll_struct.fd == STDIN_FILENO) {
                    size_t n = read(STDIN_FILENO, read_buf, read_buf_sz);
                    if (n == -1) {
                        // TODO
                        unexpected_panic_with_errno("read");
                    }
                    if (write(master_pty_fd, read_buf, n) != n) {
                        // TODO
                        unexpected_panic_with_errno("write");
                    }
                } else if (poll_struct.fd == master_pty_fd) {
                    size_t n = read(master_pty_fd, read_buf, read_buf_sz);
                    if (n == -1) {
                        bool master_pty_closed_by_child_terminating =
                            exit_status != -1 &&
                            errno == EIO;
                        if (!master_pty_closed_by_child_terminating) {
                            // TODO
                            unexpected_panic_with_errno("read");
                        }
                    }
                    if (write(STDOUT_FILENO, read_buf, n) != n) {
                        // TODO
                        unexpected_panic_with_errno("write");
                    }
                    if (write(test_fd, read_buf, n) != n) {
                        // TODO
                        unexpected_panic_with_errno("write");
                    }
                }
            }
        }
    }

    //
    // Cleanup
    //
    term_set_termios(&orig_termios);

    exit(exit_status);
}

