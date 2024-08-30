#!/usr/bin/env python3

import base64
import os
import pty
import ssl
import sys
import termios

PRIVKEY_FILE  = "ssl.priv.key"
SSL_CERT_FILE = "ssl.crt"
CHUNK_SIZE = 1024

STDIN_FILENO  = 0
STDOUT_FILENO = 1
STDERR_FILENO = 2

if len(sys.argv) < 2:
    raise Exception("Expected at least one argument!")
is_host  = sys.argv[1] == "host"
is_guest = sys.argv[1] == "guest"
if not (is_host or is_guest):
    raise Exception("First argument must be either 'host' or 'guest'!")
ip   = "0.0.0.0"
port = 8443
if is_host and len(sys.argv) >= 3:
    port = int(sys.argv[2])
if is_guest:
    ip   = sys.argv[2]
    port = int(sys.argv[3])

if is_host:
    #
    # Run openssl CLI commands as child processes to generate a self-signed
    # X.509 certificate. Then create a guest invite using the certificate.
    #

    print("Generating invite...")

    # Generate the RSA private key for signing the certificate
    child_pid = os.fork()
    is_child_proc = child_pid == 0
    if is_child_proc:
        os.execlp("openssl", "openssl",
                  "genrsa",
                  "-out", PRIVKEY_FILE,
                  "4096")
    os.waitpid(child_pid, 0)

    # Create a self-signed X.509 certificate using the RSA key
    child_pid = os.fork()
    is_child_proc = child_pid == 0
    if is_child_proc:
        os.execlp("openssl", "openssl",
                  "req",
                  "-key", PRIVKEY_FILE,
                  "-out", SSL_CERT_FILE,
                  "-new", "-x509",
                  "-days", "1",
                  "-subj", "/C=/ST=/L=/O=/CN=")
    os.waitpid(child_pid, 0)

    # Base64-URL-encode the certificate bytes and use the base64 string to
    # print the invite
    ssl_cert_pem = b""
    with open(SSL_CERT_FILE, "r") as f:
        ssl_cert_pem = f.read()
    ssl_cert_der = ssl.PEM_cert_to_DER_cert(ssl_cert_pem)
    ssl_cert_urlb64 = base64.urlsafe_b64encode(ssl_cert_der).decode()
    print(f"Invite:\n\t./impl.py guest <host IP> {port} {ssl_cert_urlb64}")

# Determine shell command
shell = os.getenv("SHELL", "sh")

# Capture the original terminal configuration
tc_attrs      = termios.tcgetattr(STDIN_FILENO)
term_win_size = termios.tcgetwinsize(STDIN_FILENO)

# Fork the process, create a psuedoterminal and connect the child process to
# the psuedoterminal's slave device as its controlling terminal
child_pid, pty_master_fd = pty.fork()
is_child_proc  = child_pid == 0
is_parent_proc = not is_child_proc

if is_child_proc:
    # Set the psuedoterminal's configuration to the same configuration as the
    # original terminal
    termios.tcsetattr(STDIN_FILENO , termios.TCSANOW, tc_attrs)
    termios.tcsetattr(STDOUT_FILENO, termios.TCSANOW, tc_attrs)
    termios.tcsetwinsize(STDIN_FILENO , term_win_size)
    termios.tcsetwinsize(STDOUT_FILENO, term_win_size)

    # Execute a subshell in child process
    os.execlp(shell, shell)

if is_parent_proc:
    import select
    import signal
    import socket
    import tty

    #
    # Check if the child process exits by trapping the SIGCHLD signal. When the
    # child exits, set `exit_status` to a non-negative value, causing the main
    # loop of the host or client to finish.
    #

    exit_status = -1

    def handle_sigchld(*args):
        global exit_status

        child_wait_status = os.waitpid(child_pid, os.WNOHANG)[1]

        # Ignore signals for suspending and unsuspending the child process
        if os.WIFSTOPPED(child_wait_status) or os.WIFCONTINUED(child_wait_status):
            return

        exit_status = 0
        # The INTR -- generally "^D" -- and KILL termios special control
        # characters should cause the child process to terminate.
        if os.WIFEXITED(child_wait_status):
            exit_status = os.WEXITSTATUS(child_wait_status)
        # External commands such as `kill` may send signals to the child
        # process before it exits or is forcefully terminated
        elif os.WIFSIGNALED(child_wait_status):
            term_sig = os.WTERMSIG(child_wait_status)
            os.write(STDERR_FILENO,
                     b"Subshell terminated unexpectedly by signal " +
                     str(term_sig).encode() + b"!")
            exit_status = 1

    signal.signal(signal.SIGCHLD, handle_sigchld)

    socks: list[socket.SocketType] = []

    #
    # THE CORE LOGIC FOR HOST AND CLIENT is contained within the try-block!
    #

    try:
        # Disable special terminal I/O processing by setting stdin and stdout
        # to raw
        tty.setraw(STDIN_FILENO)
        tty.setraw(STDOUT_FILENO)

        if is_host:
            #
            # The host runs a TLS server, accepting connections from the guest
            # clients.
            #
            # Input from the standard input, psuedoterminal and guest clients
            # is multiplexed. Output is forwarded to the relevant destination
            # while blocking (for simplicity).
            #
            # Input is forwarded as follows:
            #   STDIN             -> psuedoterminal
            #   guest connections -> psuedoterminal
            # Output is forwarded as follows:
            #   psuedoterminal -> STDOUT
            #   psuedoterminal -> guest connections
            #

            # Setup TLS server, listening for connections

            ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ssl_ctx.load_cert_chain(certfile=SSL_CERT_FILE, keyfile=PRIVKEY_FILE)

            listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
            listen_sock.bind((ip, port))
            listen_sock.listen()
            listen_sock = ssl_ctx.wrap_socket(listen_sock, server_side=True)
            socks.append(listen_sock)

            def conn_socks():
                return socks[1:]

            # HOST MAIN LOOP
            while exit_status == -1:
                # Do input multiplexing
                ready_for_read = select.select(
                    [STDIN_FILENO, pty_master_fd] + socks,
                    [],
                    [],
                )[0]

                # Accept incomming connections
                if listen_sock in ready_for_read:
                    try:
                        conn_sock, addr = listen_sock.accept()
                        socks.append(conn_sock)
                        conn_sock.sendall(b"$ ")
                    # Ignore connections where SSL verification fails
                    except ssl.SSLError:
                        pass

                # Forward the standard input to the psuedoterminal
                if STDIN_FILENO in ready_for_read:
                    stdin = os.read(STDIN_FILENO, CHUNK_SIZE)
                    os.write(pty_master_fd, stdin)
                # Receive standard input from connection sockets
                for conn_sock in conn_socks():
                    if conn_sock in ready_for_read:
                        stdin = conn_sock.recv(CHUNK_SIZE)

                        # Remove connection socket if EOT byte is received
                        END_OF_TRANSMISSION_BYTE = b'\x04'
                        end_idx = stdin.find(END_OF_TRANSMISSION_BYTE)
                        if end_idx > -1:
                            stdin = stdin[:end_idx]
                            conn_sock.close()
                            socks.remove(conn_sock)

                        os.write(pty_master_fd, stdin)

                # Read standard output from psuedoterminal, forward it to the
                # parent's stdout and to the connection sockets
                if pty_master_fd in ready_for_read:
                    stdout = os.read(pty_master_fd, CHUNK_SIZE)
                    os.write(STDOUT_FILENO, stdout)
                    for conn_sock in conn_socks():
                        conn_sock.sendall(stdout)

        if is_guest:
            #
            # The guest client connects to the host TLS server. It forwards its
            # standard input to the server and receives its standard output
            # from the server.
            #

            # Reconstruct the X.509 PEM certificate from the last CLI argument,
            # as supplied by the invite
            if len(sys.argv) != 5:
                raise Exception("Guest expects 4 CLI arguments!")
            ssl_cert_der_urlb64 = sys.argv[4]
            ssl_cert_der = base64.urlsafe_b64decode(ssl_cert_der_urlb64)
            ssl_cert_pem = ssl.DER_cert_to_PEM_cert(ssl_cert_der)

            # Setup TLS client connection to the host server

            ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            ssl_ctx.load_verify_locations(cadata=ssl_cert_der)
            ssl_ctx.verify_mode = ssl.CERT_REQUIRED
            ssl_ctx.check_hostname = False

            conn_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
            conn_sock = ssl_ctx.wrap_socket(conn_sock,
                                            server_side=False)
            conn_sock.connect((ip, port))

            # CLIENT MAIN LOOP
            while exit_status == -1:
                # Do input multiplexing
                ready_for_read = select.select(
                    [STDIN_FILENO, conn_sock],
                    [],
                    [],
                )[0]

                # Forward standard input to the host server
                if STDIN_FILENO in ready_for_read:
                    stdin = os.read(STDIN_FILENO, CHUNK_SIZE)
                    conn_sock.sendall(stdin)

                # Receive standard output from the host server
                if conn_sock in ready_for_read:
                    stdout = conn_sock.recv(CHUNK_SIZE)

                    # Break out of the loop when the host side of the
                    # connection closes
                    if len(stdout) == 0:
                        break

                    os.write(STDOUT_FILENO, stdout)

    except OSError as error:
        if "Input/output" not in str(error):
            raise error
    finally:
        #
        # Cleanup by closing sockets and restoring the terminal to its original
        # configuration.
        #

        errors = []

        for sock in socks:
            try:
                sock.shutdown(socket.SHUT_RDWR)
                sock.close()
            except Exception as error:
                errors.append(error)

        # Restore terminal configuration to original from raw
        termios.tcsetattr(STDIN_FILENO , termios.TCSANOW, tc_attrs)
        termios.tcsetattr(STDOUT_FILENO, termios.TCSANOW, tc_attrs)

        if errors:
            raise errors[0]

    exit(exit_status)

