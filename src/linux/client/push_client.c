#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

// Globals
int serial_fd = -1;
int sock_fd = -1;
int is_running = 1;

// RFC 2217 Definitions
#define IAC  255
#define SB   250
#define SE   240
#define COM_PORT_OPTION 44
#define SET_BAUDRATE 1

// Helper to translate integer baud to speed_t
speed_t get_baud(int baud) {
    switch(baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        #ifdef B460800
        case 460800: return B460800;
        #endif
        #ifdef B921600
        case 921600: return B921600;
        #endif
        default: return B9600;
    }
}

// Set baud rate dynamically
void set_baud_rate(int baudRate) {
    if (serial_fd < 0) return;

    struct termios tty;
    if (tcgetattr(serial_fd, &tty) != 0) {
        perror("tcgetattr");
        return;
    }

    speed_t speed = get_baud(baudRate);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    if (tcsetattr(serial_fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
    } else {
        printf("Baud Rate changed to %d\n", baudRate);
    }
}

// Setup Serial Port
int setup_serial(const char *portName, int baudRate) {
    serial_fd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    if (serial_fd < 0) {
        perror("Error opening serial port");
        return 0;
    }

    struct termios tty;
    if (tcgetattr(serial_fd, &tty) != 0) {
        perror("tcgetattr");
        return 0;
    }

    speed_t speed = get_baud(baudRate);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit chars
    tty.c_iflag &= ~IGNBRK;                     // disable break processing
    tty.c_lflag = 0;                            // no signaling chars, no echo, no canonical processing
    tty.c_oflag = 0;                            // no remapping, no delays
    tty.c_cc[VMIN]  = 1;                        // read doesn't block
    tty.c_cc[VTIME] = 5;                        // 0.5 seconds read timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);     // shut off xon/xoff ctrl
    tty.c_cflag |= (CLOCAL | CREAD);            // ignore modem controls, enable reading
    tty.c_cflag &= ~(PARENB | PARODD);          // shut off parity
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(serial_fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return 0;
    }
    return 1;
}

// Thread: Read from Serial -> Send to TCP
void *serial_to_tcp_thread(void *arg) {
    (void)arg;
    char buffer[4096];
    int bytesRead;

    while (is_running && sock_fd >= 0) {
        bytesRead = read(serial_fd, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            if (send(sock_fd, buffer, bytesRead, 0) < 0) {
                perror("Socket send failed");
                is_running = 0;
                break;
            }
        } else if (bytesRead < 0) {
            // Error or non-blocking empty read
            usleep(10000); 
        } else {
             // 0 means EOF usually not for serial, but just in case
             usleep(10000);
        }
    }
    return NULL;
}

// Process TCP Data (Handle RFC 2217)
void process_tcp_data(unsigned char *data, int len) {
    static int state = 0;
    static unsigned char command_buffer[16];
    static int cmd_idx = 0;
    
    for (int i = 0; i < len; i++) {
        unsigned char c = data[i];

        if (state == 0) {
            if (c == IAC) {
                state = 1;
            } else {
                write(serial_fd, &c, 1);
            }
        } else if (state == 1) { // IAC
            if (c == SB) {
                state = 2;
            } else if (c == IAC) {
                write(serial_fd, &c, 1);
                state = 0;
            } else {
                state = 0;
            }
        } else if (state == 2) { // SB
            if (c == COM_PORT_OPTION) {
                state = 3;
                cmd_idx = 0;
            } else {
                state = 0;
            }
        } else if (state == 3) { // OPTION
            if (c == SET_BAUDRATE) {
                state = 4;
            } else {
                if (c == IAC) state = 9;
                else state = 3;
            }
        } else if (state == 4) { // READING BAUD
            if (c == IAC) {
                state = 9;
            } else {
                command_buffer[cmd_idx++] = c;
                if (cmd_idx == 4) {
                    uint32_t baud = (command_buffer[0] << 24) | 
                                    (command_buffer[1] << 16) | 
                                    (command_buffer[2] << 8)  | 
                                    command_buffer[3];
                    set_baud_rate(baud);
                    state = 9;
                }
            }
        } else if (state == 9) { // WAIT FOR SE
             if (c == SE) state = 0;
        }
    }
}

int main(int argc, char *argv[]) {
    char *hub_ip = "127.0.0.1";
    int hub_port = 9000;
    char *com_port = "/dev/ttyUSB0";
    int baud = 115200;

    if (argc == 5) {
        hub_ip = argv[1];
        hub_port = atoi(argv[2]);
        com_port = argv[3];
        baud = atoi(argv[4]);
    } else if (argc == 2) {
        com_port = argv[1];
        printf("Using default Server: %s:%d and Baud: %d\n", hub_ip, hub_port, baud);
    } else if (argc == 1) {
        printf("Using defaults: %s:%d %s %d\n", hub_ip, hub_port, com_port, baud);
    } else {
        printf("Usage:\n");
        printf("  %s <HUB_IP> <HUB_PORT> <LOCAL_COM> <BAUD>\n", argv[0]);
        printf("  %s <LOCAL_COM> (Defaults to 127.0.0.1:9000)\n", argv[0]);
        return 1;
    }

    while (1) {
        printf("Connecting to Hub %s:%d...\n", hub_ip, hub_port);
        
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(hub_port);
        serverAddr.sin_addr.s_addr = inet_addr(hub_ip);

        if (connect(sock_fd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
            perror("Connection failed");
            close(sock_fd);
            sleep(5);
            continue;
        }

        printf("Connected to Hub.\n");

        // Handshake
        char handshake[256];
        snprintf(handshake, sizeof(handshake), "REGISTER:%s\n", com_port);
        send(sock_fd, handshake, strlen(handshake), 0);

        // Open Serial
        if (!setup_serial(com_port, baud)) {
            close(sock_fd);
            sleep(5);
            continue;
        }

        printf("Serial Port %s Opened.\n", com_port);
        is_running = 1;

        pthread_t tid;
        if (pthread_create(&tid, NULL, serial_to_tcp_thread, NULL) != 0) {
            perror("Failed to create thread");
            close(serial_fd);
            close(sock_fd);
            continue;
        }

        // Main Loop: TCP -> Serial
        unsigned char buffer[4096];
        int bytesReceived;

        while (is_running) {
            bytesReceived = recv(sock_fd, buffer, sizeof(buffer), 0);
            if (bytesReceived > 0) {
                process_tcp_data(buffer, bytesReceived);
            } else {
                break; // Disconnected
            }
        }

        is_running = 0;
        pthread_join(tid, NULL);
        close(serial_fd);
        close(sock_fd);
        
        printf("Link lost. Reconnecting...\n");
        sleep(2);
    }

    return 0;
}
