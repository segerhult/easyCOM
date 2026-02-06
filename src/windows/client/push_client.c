#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#pragma comment(lib, "ws2_32.lib")

// Global handles
HANDLE hSerial = INVALID_HANDLE_VALUE;
SOCKET sock = INVALID_SOCKET;
int is_running = 1;

// RFC 2217 Definitions
#define IAC  255
#define SB   250
#define SE   240
#define COM_PORT_OPTION 44
#define SET_BAUDRATE 1
#define SET_DATASIZE 2
#define SET_PARITY   3
#define SET_STOPSIZE 4

// Function to set baud rate dynamically
void set_baud_rate(int baudRate) {
    if (hSerial == INVALID_HANDLE_VALUE) return;

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        printf("Failed to get comm state\n");
        return;
    }

    dcbSerialParams.BaudRate = baudRate;
    
    if (SetCommState(hSerial, &dcbSerialParams)) {
        printf("Baud Rate changed to %d\n", baudRate);
    } else {
        printf("Failed to set Baud Rate to %d\n", baudRate);
    }
}

// Serial configuration
int setup_serial(const char *portName, int baudRate) {
    char adjustedPortName[32];
    sprintf(adjustedPortName, "\\\\.\\%s", portName);

    hSerial = CreateFile(adjustedPortName,
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);

    if (hSerial == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error opening serial port %s\n", portName);
        return 0;
    }

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        return 0;
    }

    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        return 0;
    }

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 10;
    timeouts.ReadTotalTimeoutConstant = 10;
    timeouts.ReadTotalTimeoutMultiplier = 1;
    timeouts.WriteTotalTimeoutConstant = 10;
    timeouts.WriteTotalTimeoutMultiplier = 1;

    SetCommTimeouts(hSerial, &timeouts);
    return 1;
}

// Read from Serial -> Send to TCP
DWORD WINAPI SerialToTcpThread(LPVOID lpParam) {
    (void)lpParam;
    char buffer[4096];
    DWORD bytesRead;

    while (is_running && sock != INVALID_SOCKET) {
        if (ReadFile(hSerial, buffer, sizeof(buffer), &bytesRead, NULL)) {
            if (bytesRead > 0) {
                if (send(sock, buffer, bytesRead, 0) == SOCKET_ERROR) {
                    printf("Server disconnected.\n");
                    is_running = 0;
                    break;
                }
            }
        } else {
            Sleep(10);
        }
    }
    return 0;
}

// Telnet State Machine
void process_tcp_data(unsigned char *data, int len) {
    static int state = 0; // 0: Normal, 1: IAC, 2: SB, 3: OPTION, 4: CMD, 5-8: DATA
    static unsigned char command_buffer[16];
    static int cmd_idx = 0;
    
    DWORD bytesWritten;
    
    for (int i = 0; i < len; i++) {
        unsigned char c = data[i];

        if (state == 0) {
            if (c == IAC) {
                state = 1;
            } else {
                WriteFile(hSerial, &c, 1, &bytesWritten, NULL);
            }
        } else if (state == 1) { // Received IAC
            if (c == SB) {
                state = 2;
            } else if (c == IAC) { // Double IAC = literal 255
                WriteFile(hSerial, &c, 1, &bytesWritten, NULL);
                state = 0;
            } else {
                // Other Telnet command (WILL/WONT/DO/DONT), ignore for now
                state = 0; 
            }
        } else if (state == 2) { // Received SB
            if (c == COM_PORT_OPTION) {
                state = 3;
                cmd_idx = 0;
            } else {
                state = 0; // Not COM option
            }
        } else if (state == 3) { // Received COM_PORT_OPTION
            if (c == SET_BAUDRATE) {
                state = 4;
            } else {
                 // Other sub-option, skip until SE
                if (c == IAC) state = 9; // Wait for SE
                else state = 3; // Stay here
            }
        } else if (state == 4) { // Reading Baud Rate (4 bytes)
            if (c == IAC) {
                // Unexpected IAC, maybe end?
                state = 9;
            } else {
                command_buffer[cmd_idx++] = c;
                if (cmd_idx == 4) {
                    // We have 4 bytes of baud rate (Big Endian)
                    uint32_t baud = (command_buffer[0] << 24) | 
                                    (command_buffer[1] << 16) | 
                                    (command_buffer[2] << 8)  | 
                                    command_buffer[3];
                    set_baud_rate(baud);
                    state = 9; // Wait for SE
                }
            }
        } else if (state == 9) { // Wait for SE
             if (c == SE) {
                 state = 0;
             } else if (c == IAC) {
                 // Next char should be SE
             }
        }
    }
}


int main(int argc, char *argv[]) {
    char *hub_ip = "127.0.0.1";
    int hub_port = 9000;
    char *com_port = "COM3";
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
        printf("  %s                                     (Use defaults: %s:%d %s %d)\n", argv[0], hub_ip, hub_port, com_port, baud);
        printf("  %s <LOCAL_COM>                         (Use default Server: %s:%d)\n", argv[0], hub_ip, hub_port);
        printf("  %s <HUB_IP> <HUB_PORT> <LOCAL_COM> <BAUD>\n", argv[0]);
        return 1;
    }

    WSADATA wsaData;

    while (1) {
        printf("Connecting to Hub %s:%d...\n", hub_ip, hub_port);
        
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(hub_port);
        serverAddr.sin_addr.s_addr = inet_addr(hub_ip);

        if (connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            printf("Connection failed. Retrying in 5s...\n");
            closesocket(sock);
            Sleep(5000);
            continue;
        }

        printf("Connected to Hub.\n");

        // Send Handshake
        char handshake[256];
        snprintf(handshake, sizeof(handshake), "REGISTER:%s\n", com_port);
        send(sock, handshake, strlen(handshake), 0);

        // Open Serial
        if (!setup_serial(com_port, baud)) {
            printf("Failed to open serial port %s. Retrying in 5s...\n", com_port);
            closesocket(sock);
            Sleep(5000);
            continue;
        }

        printf("Serial Port %s Opened.\n", com_port);
        is_running = 1;

        // Start Serial Reader Thread
        HANDLE hThread = CreateThread(NULL, 0, SerialToTcpThread, NULL, 0, NULL);

        // Main Loop: TCP -> Serial
        unsigned char buffer[4096];
        int bytesReceived;

        while (is_running) {
            bytesReceived = recv(sock, (char*)buffer, sizeof(buffer), 0);
            if (bytesReceived > 0) {
                process_tcp_data(buffer, bytesReceived);
            } else {
                break; // Disconnected
            }
        }

        is_running = 0;
        WaitForSingleObject(hThread, 1000);
        CloseHandle(hThread);
        CloseHandle(hSerial);
        closesocket(sock);
        
        printf("Link lost. Reconnecting...\n");
        Sleep(2000);
    }

    WSACleanup();
    return 0;
}
