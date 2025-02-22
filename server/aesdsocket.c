#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h> // For getaddrinfo
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>

#define PORT "9000" // Port as a string for getaddrinfo
#define BUFFER_SIZE 1024
#define DATA_FILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t stop_flag = 0;

// Signal handler for SIGINT and SIGTERM
void handle_signal(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        stop_flag = 1;
        syslog(LOG_INFO, "Caught signal, exiting");
        printf("Caught signal %d, exiting...\n", signum);
    }
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
        printf("Running in daemon mode.\n");
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    int server_fd, client_fd;
    struct addrinfo hints, *res;
    struct sockaddr_storage client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    FILE *data_file = NULL;

    // Configure hints for getaddrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP socket
    hints.ai_flags = AI_PASSIVE;    // Use my IP address

    // Get address information
    int status = getaddrinfo(NULL, PORT, &hints, &res);
    if (status != 0) {
        syslog(LOG_ERR, "getaddrinfo failed: %s", gai_strerror(status));
        printf("getaddrinfo failed: %s\n", gai_strerror(status));
        return -1;
    }

    // Create socket
    server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        printf("Failed to create socket: %s\n", strerror(errno));
        freeaddrinfo(res);
        return -1;
    }

    // Set SO_REUSEADDR option
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "Failed to set SO_REUSEADDR: %s", strerror(errno));
        printf("Failed to set SO_REUSEADDR: %s\n", strerror(errno));
        close(server_fd);
        freeaddrinfo(res);
        return -1;
    }

    // Bind socket to port
    if (bind(server_fd, res->ai_addr, res->ai_addrlen) == -1) {
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
        printf("Failed to bind socket: %s\n", strerror(errno));
        close(server_fd);
        freeaddrinfo(res);
        return -1;
    }

    // Free the address info structure
    freeaddrinfo(res);

    // Listen for connections
    if (listen(server_fd, 5) == -1) {
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
        printf("Failed to listen on socket: %s\n", strerror(errno));
        close(server_fd);
        return -1;
    }

    // Daemonize if requested
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Failed to fork: %s", strerror(errno));
            printf("Failed to fork: %s\n", strerror(errno));
            return -1;
        }
        if (pid > 0) {
            // Parent process exits
            return 0;
        }
        // Child process continues
        chdir("/");
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    // Handle signals
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    while (!stop_flag) {
        // Use select to make accept non-blocking
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        struct timeval timeout = {1, 0}; // 1 second timeout
        int ready = select(server_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready == -1) {
            if (errno == EINTR) continue; // Interrupted by signal
            syslog(LOG_ERR, "select failed: %s", strerror(errno));
            printf("select failed: %s\n", strerror(errno));
            break;
        }

        if (ready == 0) continue; // Timeout, check stop_flag

        // Accept connection
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            printf("Failed to accept connection: %s\n", strerror(errno));
            continue;
        }

        // Log client IP
        char client_ip[INET6_ADDRSTRLEN];
        if (client_addr.ss_family == AF_INET) {
            // IPv4
            struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
            inet_ntop(AF_INET, &s->sin_addr, client_ip, sizeof(client_ip));
        } else {
            // IPv6
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&client_addr;
            inet_ntop(AF_INET6, &s->sin6_addr, client_ip, sizeof(client_ip));
        }
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
        printf("Accepted connection from %s\n", client_ip);

        // Receive data and append to file
        data_file = fopen(DATA_FILE, "a");
        if (!data_file) {
            syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
            printf("Failed to open data file: %s\n", strerror(errno));
            close(client_fd);
            continue;
        }

        while ((bytes_read = read(client_fd, buffer, BUFFER_SIZE)) > 0) {
            fwrite(buffer, 1, bytes_read, data_file);
            if (memchr(buffer, '\n', bytes_read)) {
                break; // End of packet
            }
        }
        fclose(data_file);

        // Send file content back to client
        data_file = fopen(DATA_FILE, "r");
        if (!data_file) {
            syslog(LOG_ERR, "Failed to open data file for reading: %s", strerror(errno));
            printf("Failed to open data file for reading: %s\n", strerror(errno));
            close(client_fd);
            continue;
        }

        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, data_file)) > 0) {
            write(client_fd, buffer, bytes_read);
        }
        fclose(data_file);

        // Log connection closure
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        printf("Closed connection from %s\n", client_ip);
        close(client_fd);
    }

    // Cleanup
    close(server_fd);
    remove(DATA_FILE);
    closelog();
    printf("Server shutdown gracefully.\n");

    return 0;
}