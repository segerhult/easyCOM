#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#define BUFFER_SIZE 4096

// Function to handle signal for zombie processes
void sigchld_handler(int s) {
    (void)s; // quiet unused variable warning
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

// Function to create a connection to the target
int connect_to_target(const char *hostname, int port) {
    struct hostent *host;
    struct sockaddr_in server_addr;
    int sock;

    host = gethostbyname(hostname);
    if (host == NULL) {
        herror("gethostbyname");
        return -1;
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr = *((struct in_addr *)host->h_addr);
    memset(&(server_addr.sin_zero), 0, 8);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        perror("connect");
        close(sock);
        return -1;
    }

    return sock;
}

// Function to forward data between two sockets
void forward_data(int client_sock, int target_sock) {
    fd_set read_fds;
    int max_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;

    max_fd = (client_sock > target_sock) ? client_sock : target_sock;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(client_sock, &read_fds);
        FD_SET(target_sock, &read_fds);

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) == -1) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // Data from Client -> Target
        if (FD_ISSET(client_sock, &read_fds)) {
            bytes_read = recv(client_sock, buffer, BUFFER_SIZE, 0);
            if (bytes_read <= 0) break; // Connection closed or error
            
            // Write all data
            ssize_t total_written = 0;
            while (total_written < bytes_read) {
                bytes_written = send(target_sock, buffer + total_written, bytes_read - total_written, 0);
                if (bytes_written <= 0) {
                     goto end_forward;
                }
                total_written += bytes_written;
            }
        }

        // Data from Target -> Client
        if (FD_ISSET(target_sock, &read_fds)) {
            bytes_read = recv(target_sock, buffer, BUFFER_SIZE, 0);
            if (bytes_read <= 0) break;

            ssize_t total_written = 0;
            while (total_written < bytes_read) {
                bytes_written = send(client_sock, buffer + total_written, bytes_read - total_written, 0);
                if (bytes_written <= 0) {
                     goto end_forward;
                }
                total_written += bytes_written;
            }
        }
    }

end_forward:
    return;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <local_port> <target_host> <target_port>\n", argv[0]);
        fprintf(stderr, "Example: %s 8080 127.0.0.1 22\n", argv[0]);
        exit(1);
    }

    int local_port = atoi(argv[1]);
    char *target_host = argv[2];
    int target_port = atoi(argv[3]);

    int sockfd, new_fd;
    struct sockaddr_in my_addr, their_addr;
    socklen_t sin_size;
    struct sigaction sa;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(local_port);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(my_addr.sin_zero), 0, 8);

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind");
        exit(1);
    }

    if (listen(sockfd, 10) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("TCP Forwarder Running.\n");
    printf("Listening on 0.0.0.0:%d\n", local_port);
    printf("Forwarding to %s:%d\n", target_host, target_port);

    while (1) {
        sin_size = sizeof(struct sockaddr_in);
        if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size)) == -1) {
            perror("accept");
            continue;
        }

        printf("Connection accepted from %s\n", inet_ntoa(their_addr.sin_addr));

        if (!fork()) { // Child process
            close(sockfd); // Child doesn't need the listener
            int target_sock = connect_to_target(target_host, target_port);
            if (target_sock != -1) {
                forward_data(new_fd, target_sock);
                close(target_sock);
                printf("Connection closed for %s\n", inet_ntoa(their_addr.sin_addr));
            } else {
                fprintf(stderr, "Failed to connect to target %s:%d\n", target_host, target_port);
            }
            close(new_fd);
            exit(0);
        }
        close(new_fd); // Parent doesn't need this
    }

    return 0;
}
