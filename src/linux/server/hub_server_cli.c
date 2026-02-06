#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_CLIENTS 100

typedef struct {
    int sock;
    char name[64];
    int id;
    int active;
    int bridge_sock; // Socket for external app to connect to (if mapped)
    int bridge_port;
} ClientInfo;

ClientInfo clients[MAX_CLIENTS];
pthread_mutex_t cs = PTHREAD_MUTEX_INITIALIZER;

// Bridge Thread: Reads from External App -> Writes to Device
void *bridge_to_device_thread(void *arg) {
    int id = (int)(intptr_t)arg;
    char buffer[4096];
    int bytesReceived;

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(clients[id].bridge_port);
    
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("[Error] Could not bind bridge port %d\n", clients[id].bridge_port);
        close(listen_sock);
        return NULL;
    }
    listen(listen_sock, 1);
    
    printf("[Bridge] Listening on port %d for Device %d\n", clients[id].bridge_port, id);

    while (clients[id].active) {
        int app_sock = accept(listen_sock, NULL, NULL);
        if (app_sock < 0) continue;
        
        printf("[Bridge] App connected to Device %d\n", id);
        clients[id].bridge_sock = app_sock;

        // Loop: App -> Device
        while (clients[id].active) {
            bytesReceived = recv(app_sock, buffer, sizeof(buffer), 0);
            if (bytesReceived <= 0) break;
            send(clients[id].sock, buffer, bytesReceived, 0);
        }
        
        close(app_sock);
        clients[id].bridge_sock = -1;
        printf("[Bridge] App disconnected from Device %d\n", id);
    }
    
    close(listen_sock);
    return NULL;
}

// Client Handler Thread: Reads from Device -> Writes to Bridge
void *client_handler(void *arg) {
    int id = (int)(intptr_t)arg;
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
        if (clients[id].bridge_sock != -1) {
            send(clients[id].bridge_sock, buffer, bytesReceived, 0);
        }
    }

    pthread_mutex_lock(&cs);
    clients[id].active = 0;
    close(clients[id].sock);
    pthread_mutex_unlock(&cs);
    return NULL;
}

void *accept_thread(void *arg) {
    int listenSock = (int)(intptr_t)arg;
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    while (1) {
        int clientSock = accept(listenSock, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSock < 0) continue;

        pthread_mutex_lock(&cs);
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
            clients[id].bridge_sock = -1;
            clients[id].bridge_port = 0;
            strcpy(clients[id].name, "Unknown");
            
            printf("New Connection ID: %d from %s\n", id, inet_ntoa(clientAddr.sin_addr));
            
            pthread_t tid;
            pthread_create(&tid, NULL, client_handler, (void*)(intptr_t)id);
            pthread_detach(tid);
        } else {
            printf("Max clients reached. Rejected.\n");
            close(clientSock);
        }
        pthread_mutex_unlock(&cs);
    }
    return NULL;
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

    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    
    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listenSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        printf("Bind failed on port %d.\n", port);
        return 1;
    }
    listen(listenSock, 5);

    printf("Hub Server Listening on %d\n", port);
    printf("Commands:\n");
    printf("  list              - List devices\n");
    printf("  map <id> <port>   - Map device <id> to local TCP <port>\n");

    pthread_t tid;
    pthread_create(&tid, NULL, accept_thread, (void*)(intptr_t)listenSock);
    pthread_detach(tid);

    char cmd[128];
    while (1) {
        printf("> ");
        if (!fgets(cmd, sizeof(cmd), stdin)) break;

        if (strncmp(cmd, "list", 4) == 0) {
            pthread_mutex_lock(&cs);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].active) {
                    printf("ID: %d | Name: %s | Mapped: %s\n", 
                        i, clients[i].name, 
                        clients[i].bridge_port ? "Yes" : "No");
                }
            }
            pthread_mutex_unlock(&cs);
        } else if (strncmp(cmd, "map", 3) == 0) {
            int id, port;
            if (sscanf(cmd, "map %d %d", &id, &port) == 2) {
                if (id >= 0 && id < MAX_CLIENTS && clients[id].active) {
                    clients[id].bridge_port = port;
                    
                    pthread_t btid;
                    pthread_create(&btid, NULL, bridge_to_device_thread, (void*)(intptr_t)id);
                    pthread_detach(btid);
                    
                    printf("Mapped Device %d to Port %d\n", id, port);
                } else {
                    printf("Invalid Device ID\n");
                }
            }
        }
    }

    return 0;
}
