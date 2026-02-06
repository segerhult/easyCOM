#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_CLIENTS 100

typedef struct {
    SOCKET sock;
    char name[64];
    int id;
    int active;
    SOCKET bridge_sock; // Socket for external app to connect to (if mapped)
    int bridge_port;
} ClientInfo;

ClientInfo clients[MAX_CLIENTS];
CRITICAL_SECTION cs;

// Bridge Thread: Reads from External App -> Writes to Device
DWORD WINAPI BridgeToDeviceThread(LPVOID lpParam) {
    int id = (int)(intptr_t)lpParam;
    char buffer[4096];
    int bytesReceived;

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(clients[id].bridge_port);
    
    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("[Error] Could not bind bridge port %d\n", clients[id].bridge_port);
        closesocket(listen_sock);
        return 0;
    }
    listen(listen_sock, 1);
    
    printf("[Bridge] Listening on port %d for Device %d\n", clients[id].bridge_port, id);

    while (clients[id].active) {
        SOCKET app_sock = accept(listen_sock, NULL, NULL);
        if (app_sock == INVALID_SOCKET) continue;
        
        printf("[Bridge] App connected to Device %d\n", id);
        clients[id].bridge_sock = app_sock;

        // Loop: App -> Device
        while (clients[id].active) {
            bytesReceived = recv(app_sock, buffer, sizeof(buffer), 0);
            if (bytesReceived <= 0) break;
            send(clients[id].sock, buffer, bytesReceived, 0);
        }
        
        closesocket(app_sock);
        clients[id].bridge_sock = INVALID_SOCKET;
        printf("[Bridge] App disconnected from Device %d\n", id);
    }
    
    closesocket(listen_sock);
    return 0;
}

// Client Handler Thread: Reads from Device -> Writes to Bridge
DWORD WINAPI ClientHandler(LPVOID lpParam) {
    int id = (int)(intptr_t)lpParam;
    char buffer[4096];
    int bytesReceived;

    // Handshake
    bytesReceived = recv(clients[id].sock, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived > 0) {
        buffer[bytesReceived] = 0;
        if (strncmp(buffer, "REGISTER:", 9) == 0) {
            strncpy(clients[id].name, buffer + 9, 63);
            // Remove newline
            char *p = strchr(clients[id].name, '\n');
            if (p) *p = 0;
            printf("Client %d Registered: %s\n", id, clients[id].name);
        }
    }

    while (clients[id].active) {
        bytesReceived = recv(clients[id].sock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            printf("Client %d (%s) Disconnected.\n", id, clients[id].name);
            break;
        }

        // If bridged, forward to app
        if (clients[id].bridge_sock != INVALID_SOCKET) {
            send(clients[id].bridge_sock, buffer, bytesReceived, 0);
        }
    }

    EnterCriticalSection(&cs);
    clients[id].active = 0;
    closesocket(clients[id].sock);
    LeaveCriticalSection(&cs);
    return 0;
}

DWORD WINAPI AcceptThread(LPVOID lpParam) {
    SOCKET listenSock = (SOCKET)lpParam;
    struct sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);

    while (1) {
        SOCKET clientSock = accept(listenSock, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSock == INVALID_SOCKET) continue;

        EnterCriticalSection(&cs);
        int id = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                id = i;
                break;
            }
        }

        if (id != -1) {
            clients[id].sock = clientSock;
            clients[id].active = 1;
            clients[id].id = id;
            clients[id].bridge_sock = INVALID_SOCKET;
            clients[id].bridge_port = 0;
            strcpy(clients[id].name, "Unknown");
            
            printf("New Connection ID: %d from %s\n", id, inet_ntoa(clientAddr.sin_addr));
            CreateThread(NULL, 0, ClientHandler, (LPVOID)(intptr_t)id, 0, NULL);
        } else {
            printf("Max clients reached. Rejected.\n");
            closesocket(clientSock);
        }
        LeaveCriticalSection(&cs);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int port = 9000;
    if (argc == 2) {
        port = atoi(argv[1]);
    } else if (argc == 1) {
        printf("Using default port: %d\n", port);
    } else {
        printf("Usage: %s [LISTEN_PORT] (Default: 9000)\n", argv[0]);
        return 1;
    }

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    InitializeCriticalSection(&cs);

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(listenSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("Bind failed on port %d.\n", port);
        return 1;
    }
    listen(listenSock, 5);

    printf("Hub Server Listening on %d\n", port);
    printf("Commands:\n");
    printf("  list              - List devices\n");
    printf("  map <id> <port>   - Map device <id> to local TCP <port>\n");

    CreateThread(NULL, 0, AcceptThread, (LPVOID)listenSock, 0, NULL);

    char cmd[128];
    while (1) {
        printf("> ");
        if (!fgets(cmd, sizeof(cmd), stdin)) break;

        if (strncmp(cmd, "list", 4) == 0) {
            EnterCriticalSection(&cs);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].active) {
                    printf("ID: %d | Name: %s | Mapped: %s\n", 
                        i, clients[i].name, 
                        clients[i].bridge_port ? "Yes" : "No");
                }
            }
            LeaveCriticalSection(&cs);
        } else if (strncmp(cmd, "map", 3) == 0) {
            int id, port;
            if (sscanf(cmd, "map %d %d", &id, &port) == 2) {
                if (id >= 0 && id < MAX_CLIENTS && clients[id].active) {
                    clients[id].bridge_port = port;
                    CreateThread(NULL, 0, BridgeToDeviceThread, (LPVOID)(intptr_t)id, 0, NULL);
                    printf("Mapped Device %d to Port %d\n", id, port);
                } else {
                    printf("Invalid Device ID\n");
                }
            }
        }
    }

    return 0;
}
