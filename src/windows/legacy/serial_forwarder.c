#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")

// Global handles
HANDLE hSerial;
SOCKET clientSocket = INVALID_SOCKET;
int is_running = 1;

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
        fprintf(stderr, "Error getting serial state\n");
        CloseHandle(hSerial);
        return 0;
    }

    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        fprintf(stderr, "Error setting serial state\n");
        CloseHandle(hSerial);
        return 0;
    }

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        fprintf(stderr, "Error setting timeouts\n");
        CloseHandle(hSerial);
        return 0;
    }

    return 1;
}

// Thread: Read from Serial -> Send to TCP
unsigned __stdcall SerialToTcpThread(void *arg) {
    (void)arg;
    char buffer[4096];
    DWORD bytesRead;

    while (is_running && clientSocket != INVALID_SOCKET) {
        if (ReadFile(hSerial, buffer, sizeof(buffer), &bytesRead, NULL)) {
            if (bytesRead > 0) {
                if (send(clientSocket, buffer, bytesRead, 0) == SOCKET_ERROR) {
                    printf("TCP Send Error or Disconnect\n");
                    break;
                }
            }
        } else {
            // Read error or timeout
            Sleep(10);
        }
    }
    return 0;
}

// Thread: Read from TCP -> Write to Serial
unsigned __stdcall TcpToSerialThread(void *arg) {
    (void)arg;
    char buffer[4096];
    int bytesReceived;
    DWORD bytesWritten;

    while (is_running && clientSocket != INVALID_SOCKET) {
        bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived > 0) {
            if (!WriteFile(hSerial, buffer, bytesReceived, &bytesWritten, NULL)) {
                printf("Serial Write Error\n");
                break;
            }
        } else if (bytesReceived == 0 || bytesReceived == SOCKET_ERROR) {
            printf("TCP Client Disconnected\n");
            break;
        }
    }
    
    // Signal main loop to reset
    closesocket(clientSocket);
    clientSocket = INVALID_SOCKET;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <COM_PORT> <BAUD_RATE> <TCP_PORT>\n", argv[0]);
        printf("Example: %s COM3 115200 9999\n", argv[0]);
        return 1;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    if (!setup_serial(argv[1], atoi(argv[2]))) {
        WSACleanup();
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        return 1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(atoi(argv[3]));

    if (bind(listenSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("Bind failed\n");
        closesocket(listenSocket);
        return 1;
    }

    if (listen(listenSocket, 1) == SOCKET_ERROR) {
        printf("Listen failed\n");
        return 1;
    }

    printf("Listening on port %s...\n", argv[3]);

    while (is_running) {
        printf("Waiting for connection...\n");
        clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            printf("Accept failed\n");
            continue;
        }

        printf("Client connected!\n");

        HANDLE hThread1 = (HANDLE)_beginthreadex(NULL, 0, SerialToTcpThread, NULL, 0, NULL);
        HANDLE hThread2 = (HANDLE)_beginthreadex(NULL, 0, TcpToSerialThread, NULL, 0, NULL);

        WaitForSingleObject(hThread2, INFINITE); // Wait for TCP to disconnect
        
        // Cleanup threads
        TerminateThread(hThread1, 0); // Force kill serial reader if stuck
        CloseHandle(hThread1);
        CloseHandle(hThread2);
    }

    CloseHandle(hSerial);
    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
