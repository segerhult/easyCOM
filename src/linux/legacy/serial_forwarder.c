#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

#define BUFFER_SIZE 4096

// Function to configure serial port
int open_serial_port(const char *device, int baud_rate) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        perror("open_serial_port: Unable to open port");
        return -1;
    }

    // Make the file descriptor non-blocking (we use select)
    fcntl(fd, F_SETFL, 0);

    struct termios options;
    tcgetattr(fd, &options);

    // Set Baud Rate
    speed_t speed;
    switch (baud_rate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        default:
            fprintf(stderr, "Unsupported baud rate: %d. Defaulting to 9600.\n", baud_rate);
            speed = B9600;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    // Raw mode (8N1, no flow control, no canonical mode)
    cfmakeraw(&options);
    
    // Additional settings to ensure 8N1 and no flow control
    options.c_cflag &= ~PARENB; // No parity
    options.c_cflag &= ~CSTOPB; // 1 stop bit
    options.c_cflag &= ~CSIZE;  // Mask character size bits
    options.c_cflag |= CS8;     // 8 data bits
    options.c_cflag &= ~CRTSCTS;// No hardware flow control
    
    options.c_cflag |= (CLOCAL | CREAD); // Enable receiver, ignore modem control lines

    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

// Function to create server socket
int create_server_socket(int port) {
    int sockfd;
    struct sockaddr_in server_addr;
    int yes = 1;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(server_addr.sin_zero), 0, 8);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 1) == -1) { // Only allow 1 connection at a time for serial
        perror("listen");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <serial_device> <baud_rate> <tcp_port>\n", argv[0]);
        fprintf(stderr, "Example: %s /dev/tty.usbmodem1101 115200 9999\n", argv[0]);
        exit(1);
    }

    char *serial_device = argv[1];
    int baud_rate = atoi(argv[2]);
    int tcp_port = atoi(argv[3]);

    printf("Opening serial port %s at %d baud...\n", serial_device, baud_rate);
    int serial_fd = open_serial_port(serial_device, baud_rate);
    if (serial_fd == -1) exit(1);

    printf("Starting TCP server on port %d...\n", tcp_port);
    int server_fd = create_server_socket(tcp_port);
    if (server_fd == -1) {
        close(serial_fd);
        exit(1);
    }

    printf("Waiting for connection...\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t sin_size = sizeof(struct sockaddr_in);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &sin_size);
        
        if (client_fd == -1) {
            perror("accept");
            continue;
        }

        printf("Client connected: %s\n", inet_ntoa(client_addr.sin_addr));

        fd_set read_fds;
        int max_fd = (serial_fd > client_fd) ? serial_fd : client_fd;
        char buffer[BUFFER_SIZE];

        while (1) {
            FD_ZERO(&read_fds);
            FD_SET(serial_fd, &read_fds);
            FD_SET(client_fd, &read_fds);

            if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) == -1) {
                if (errno == EINTR) continue;
                perror("select");
                break;
            }

            // Serial -> TCP
            if (FD_ISSET(serial_fd, &read_fds)) {
                ssize_t n = read(serial_fd, buffer, BUFFER_SIZE);
                if (n > 0) {
                    if (send(client_fd, buffer, n, 0) == -1) {
                        perror("send");
                        break;
                    }
                } else if (n < 0 && errno != EAGAIN) {
                    perror("read serial");
                    // Usually don't break on serial read error, but depends on severity
                }
            }

            // TCP -> Serial
            if (FD_ISSET(client_fd, &read_fds)) {
                ssize_t n = recv(client_fd, buffer, BUFFER_SIZE, 0);
                if (n <= 0) {
                    printf("Client disconnected.\n");
                    break;
                }
                if (write(serial_fd, buffer, n) == -1) {
                    perror("write serial");
                }
            }
        }

        close(client_fd);
        printf("Waiting for new connection...\n");
    }

    close(serial_fd);
    close(server_fd);
    return 0;
}
