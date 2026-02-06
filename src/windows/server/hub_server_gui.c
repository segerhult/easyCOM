#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define MAX_CLIENTS 100
#define ID_LISTBOX 101
#define ID_BTN_MAP 102
#define WM_UPDATE_LIST (WM_USER + 1)

typedef struct {
    SOCKET sock;
    char name[64];
    int id;
    int active;
    SOCKET bridge_sock;
    int bridge_port;
} ClientInfo;

ClientInfo clients[MAX_CLIENTS];
CRITICAL_SECTION cs;
HWND hListBox;
HWND hMainWnd;

// Forward declarations
DWORD WINAPI AcceptThread(LPVOID lpParam);
DWORD WINAPI ClientHandler(LPVOID lpParam);
DWORD WINAPI BridgeToDeviceThread(LPVOID lpParam);

// Bridge Thread (Same as CLI)
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
        MessageBox(NULL, "Failed to bind bridge port", "Error", MB_OK);
        closesocket(listen_sock);
        return 0;
    }
    listen(listen_sock, 1);
    
    while (clients[id].active) {
        SOCKET app_sock = accept(listen_sock, NULL, NULL);
        if (app_sock == INVALID_SOCKET) continue;
        
        clients[id].bridge_sock = app_sock;
        // Update UI logic here if needed (e.g., change status)

        while (clients[id].active) {
            bytesReceived = recv(app_sock, buffer, sizeof(buffer), 0);
            if (bytesReceived <= 0) break;
            send(clients[id].sock, buffer, bytesReceived, 0);
        }
        
        closesocket(app_sock);
        clients[id].bridge_sock = INVALID_SOCKET;
    }
    closesocket(listen_sock);
    return 0;
}

// Client Handler (Same as CLI but updates UI)
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
            char *p = strchr(clients[id].name, '\n');
            if (p) *p = 0;
            
            // Notify UI
            PostMessage(hMainWnd, WM_UPDATE_LIST, 0, 0);
        }
    }

    while (clients[id].active) {
        bytesReceived = recv(clients[id].sock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) break;

        if (clients[id].bridge_sock != INVALID_SOCKET) {
            send(clients[id].bridge_sock, buffer, bytesReceived, 0);
        }
    }

    EnterCriticalSection(&cs);
    clients[id].active = 0;
    closesocket(clients[id].sock);
    LeaveCriticalSection(&cs);
    
    PostMessage(hMainWnd, WM_UPDATE_LIST, 0, 0);
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
            strcpy(clients[id].name, "Connecting...");
            
            CreateThread(NULL, 0, ClientHandler, (LPVOID)(intptr_t)id, 0, NULL);
        } else {
            closesocket(clientSock);
        }
        LeaveCriticalSection(&cs);
    }
    return 0;
}

void UpdateListBox() {
    SendMessage(hListBox, LB_RESETCONTENT, 0, 0);
    EnterCriticalSection(&cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            char entry[128];
            sprintf(entry, "ID: %d - %s [%s]", i, clients[i].name, 
                    clients[i].bridge_port ? "Mapped" : "Idle");
            int index = SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)entry);
            SendMessage(hListBox, LB_SETITEMDATA, index, (LPARAM)i);
        }
    }
    LeaveCriticalSection(&cs);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE:
            hListBox = CreateWindow("LISTBOX", NULL, 
                WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                10, 10, 360, 200, hwnd, (HMENU)ID_LISTBOX, NULL, NULL);
            
            CreateWindow("BUTTON", "Map Selected to Port...", 
                WS_CHILD | WS_VISIBLE,
                10, 220, 150, 30, hwnd, (HMENU)ID_BTN_MAP, NULL, NULL);
            break;
            
        case WM_UPDATE_LIST:
            UpdateListBox();
            break;
            
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_MAP) {
                int index = SendMessage(hListBox, LB_GETCURSEL, 0, 0);
                if (index != LB_ERR) {
                    int id = SendMessage(hListBox, LB_GETITEMDATA, index, 0);
                    // Simple input dialog hack: ask user for port via console or hardcode for now
                    // For a proper GUI, we'd need a DialogBox resource or InputBox
                    // Let's just pick a port based on ID for simplicity in this demo
                    int port = 10000 + id;
                    char msg[128];
                    sprintf(msg, "Mapping Client %d to Local Port %d?", id, port);
                    if (MessageBox(hwnd, msg, "Confirm Mapping", MB_YESNO) == IDYES) {
                        EnterCriticalSection(&cs);
                        clients[id].bridge_port = port;
                        CreateThread(NULL, 0, BridgeToDeviceThread, (LPVOID)(intptr_t)id, 0, NULL);
                        LeaveCriticalSection(&cs);
                        UpdateListBox();
                        
                        sprintf(msg, "Mapped! Connect to localhost:%d", port);
                        MessageBox(hwnd, msg, "Success", MB_OK);
                    }
                }
            }
            break;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    InitializeCriticalSection(&cs);

    // Start Server
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(9000); // Default Port 9000

    if (bind(listenSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        MessageBox(NULL, "Could not bind to port 9000", "Error", MB_ICONERROR);
        return 1;
    }
    listen(listenSock, 5);
    CreateThread(NULL, 0, AcceptThread, (LPVOID)listenSock, 0, NULL);

    // GUI Registration
    WNDCLASS wc = {0};
    wc.lpszClassName = "HubServerWnd";
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hMainWnd = CreateWindow("HubServerWnd", "Serial Hub Server (Port 9000)", 
        WS_OVERLAPPEDWINDOW, 
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, 
        NULL, NULL, hInstance, NULL);

    ShowWindow(hMainWnd, nCmdShow);

    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
