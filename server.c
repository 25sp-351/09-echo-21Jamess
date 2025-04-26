#include <arpa/inet.h>
#include <getopt.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_PORT 2345
#define BUFFER_SIZE 1024

// Writes all bytes to the file descriptor.
static ssize_t write_all(int fd, const void *buf, size_t count) {
    size_t bytes_written = 0;
    const char *buf_ptr  = buf;
    while (bytes_written < count) {
        ssize_t res = write(fd, buf_ptr + bytes_written, count - bytes_written);
        if (res < 0)
            return res;
        bytes_written += res;
    }
    return bytes_written;
}

static void parse_arguments(int argc, char *argv[], int *port, bool *verbose) {
    int opt;
    while ((opt = getopt(argc, argv, "p:vh")) != -1) {
        switch (opt) {
            case 'p': {
                char *endptr;
                long parsed_port = strtol(optarg, &endptr, 10);
                if (*endptr != '\0' || parsed_port < 1 || parsed_port > 65535) {
                    fprintf(stderr, "Invalid port number: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                *port = (int)parsed_port;
                break;
            }
            case 'v':
                *verbose = true;
                break;
            case 'h':
                printf("Usage: %s [-p port] [-v]\n", argv[0]);
                exit(EXIT_SUCCESS);
            default:
                fprintf(stderr, "Usage: %s [-p port] [-v]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
}

// Sets up a TCP server socket on the specified port.
static int setup_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
        0) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    return server_fd;
}

static void handle_connection(int client_fd, bool verbose) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    size_t buffer_len = 0;

    while ((bytes_read = read(client_fd, buffer + buffer_len,
                              sizeof(buffer) - buffer_len - 1)) > 0) {
        buffer_len += bytes_read;
        buffer[buffer_len] = '\0';

        // If the buffer fills up, add a newline to force a break.
        if (buffer_len == BUFFER_SIZE - 1)
            buffer[buffer_len - 1] = '\n';

        char *newline_pos;
        // Process every complete line (ending with '\n')
        while ((newline_pos = strchr(buffer, '\n')) != NULL) {
            size_t line_length = newline_pos - buffer + 1;
            if (verbose)
                printf("Received: %.*s", (int)line_length, buffer);
            if (write_all(client_fd, buffer, line_length) < 0) {
                perror("write failed");
                break;
            }
            size_t remaining = buffer_len - line_length;
            memmove(buffer, buffer + line_length, remaining);
            buffer_len = remaining;
            buffer[buffer_len] =
                '\0';  // Make sure the buffer is always a proper string.
        }
    }

    if (bytes_read < 0)
        perror("read failed");
}

// Structure to hold arguments for thread routine.
struct thread_args {
    int client_fd;
    bool verbose;
};

static void *handle_client(void *arg) {
    struct thread_args *args = (struct thread_args *)arg;
    int client_fd            = args->client_fd;
    bool verbose             = args->verbose;
    free(args);

    handle_connection(client_fd, verbose);
    close(client_fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port     = DEFAULT_PORT;
    bool verbose = false;

    parse_arguments(argc, argv, &port, &verbose);
    int server_fd = setup_server_socket(port);

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd =
            accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }

        struct thread_args *args = malloc(sizeof(*args));
        if (args == NULL) {
            perror("malloc failed");
            close(client_fd);
            continue;
        }
        args->client_fd = client_fd;
        args->verbose   = verbose;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, args) != 0) {
            perror("pthread_create failed");
            free(args);
            close(client_fd);
            continue;
        }
        pthread_detach(thread_id);
    }

    close(server_fd);
    return EXIT_SUCCESS;
}
