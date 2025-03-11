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
#include <pthread.h>
#include <time.h>

#define PORT "9000" // Port as a string for getaddrinfo
#define BUFFER_SIZE 1024
#define DATA_FILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t stop_flag = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// Signal handler for SIGINT and SIGTERM
void handle_signal(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        stop_flag = 1;
        syslog(LOG_INFO, "Caught signal, exiting");
        printf("Caught signal %d, exiting...\n", signum);
    }
}

// Thread function to handle client connections
void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg); // Free the allocated memory for client_fd
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    char *complete_packet = NULL;
    size_t packet_size = 0;

    // Read data until newline is found
    while ((bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';
        
        // Append to complete packet
        char *new_packet = realloc(complete_packet, packet_size + bytes_read + 1);
        if (!new_packet) {
            syslog(LOG_ERR, "Memory allocation failed");
            free(complete_packet);
            close(client_fd);
            return NULL;
        }
        
        complete_packet = new_packet;
        memcpy(complete_packet + packet_size, buffer, bytes_read);
        packet_size += bytes_read;
        
        // Check if packet contains newline
        if (strchr(buffer, '\n'))
            break;
    }
    
    if (bytes_read < 0) {
        syslog(LOG_ERR, "recv failed: %s", strerror(errno));
        free(complete_packet);
        close(client_fd);
        return NULL;
    }
    
    // Write to file with mutex protection
    pthread_mutex_lock(&file_mutex);
    FILE *data_file = fopen(DATA_FILE, "a+");
    if (!data_file) {
        syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
        pthread_mutex_unlock(&file_mutex);
        free(complete_packet);
        close(client_fd);
        return NULL;
    }
    
    // Write the complete packet
    if (complete_packet && packet_size > 0) {
        fwrite(complete_packet, 1, packet_size, data_file);
        fflush(data_file);
    }
    
    // Read the entire file and send back to client
    rewind(data_file);
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, data_file)) > 0) {
        if (send(client_fd, buffer, bytes_read, 0) < 0) {
            syslog(LOG_ERR, "send failed: %s", strerror(errno));
            break;
        }
    }
    
    fclose(data_file);
    pthread_mutex_unlock(&file_mutex);
    
    free(complete_packet);
    close(client_fd);
    syslog(LOG_INFO, "Closed connection from client");
    
    return NULL;
}

// Timer thread function to append timestamps
void *timestamp_thread(void *arg) {
    (void)arg; // Explicitly mark the parameter as unused
    while (!stop_flag) {
        sleep(10); // Wait for 10 seconds

        pthread_mutex_lock(&file_mutex); // Lock the mutex
        FILE *data_file = fopen(DATA_FILE, "a");
        if (!data_file) {
            syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
            pthread_mutex_unlock(&file_mutex); // Unlock the mutex
            continue;
        }

        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%a, %d %b %Y %H:%M:%S %z", localtime(&now));
        fprintf(data_file, "timestamp:%s\n", timestamp);
        fflush(data_file);
        fclose(data_file);
        pthread_mutex_unlock(&file_mutex); // Unlock the mutex
    }
    return NULL;
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

    // Create data file or truncate if it exists
    FILE *data_file = fopen(DATA_FILE, "w");
    if (!data_file) {
        syslog(LOG_ERR, "Failed to create data file: %s", strerror(errno));
        printf("Failed to create data file: %s\n", strerror(errno));
        close(server_fd);
        return -1;
    }
    fclose(data_file);

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
        if (chdir("/") != 0) {
            perror("chdir failed");
            exit(EXIT_FAILURE);
        }
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    // Handle signals
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Create timestamp thread
    pthread_t timestamp_tid;
    if (pthread_create(&timestamp_tid, NULL, timestamp_thread, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create timestamp thread");
        printf("Failed to create timestamp thread\n");
        close(server_fd);
        return -1;
    }

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

        // Create a new thread to handle the client
        pthread_t tid;
        int *client_fd_ptr = malloc(sizeof(int));
        if (!client_fd_ptr) {
            syslog(LOG_ERR, "Failed to allocate memory");
            close(client_fd);
            continue;
        }
        *client_fd_ptr = client_fd;
        if (pthread_create(&tid, NULL, handle_client, client_fd_ptr) != 0) {
            syslog(LOG_ERR, "Failed to create thread for client");
            printf("Failed to create thread for client\n");
            free(client_fd_ptr);
            close(client_fd);
            continue;
        }
        pthread_detach(tid); // Detach thread to avoid memory leaks
    }

    // Wait for timestamp thread to finish
    pthread_cancel(timestamp_tid);
    pthread_join(timestamp_tid, NULL);

    // Cleanup
    close(server_fd);
    remove(DATA_FILE);
    pthread_mutex_destroy(&file_mutex);
    closelog();
    printf("Server shutdown gracefully.\n");

    return 0;
}