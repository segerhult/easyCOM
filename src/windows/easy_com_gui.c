#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ============================================================================
// SHARED DEFINITIONS
// ============================================================================
#define ID_BTN_SERVER_MODE 1001
#define ID_BTN_CLIENT_MODE 1002

// SERVER IDs
#define ID_SERVER_LISTBOX  2001
#define ID_BTN_MAP         2002
#define WM_UPDATE_LIST     (WM_USER + 1)

// CLIENT IDs
#define ID_EDIT_IP         3001
#define ID_EDIT_PORT       3002
#define ID_EDIT_COM        3003
#define ID_EDIT_BAUD       3004
#define ID_BTN_CONNECT     3005
#define ID_LOG_AREA        3006
#define WM_LOG_MESSAGE     (WM_USER + 2)

// Global Instances
HINSTANCE hInst;
HWND hLauncherWnd;
HWND hServerWnd = NULL;
HWND hClientWnd = NULL;

// Font
HFONT hFont;

// ============================================================================
// SERVER IMPLEMENTATION
// ============================================================================
#define MAX_CLIENTS 100

typedef struct {
    SOCKET sock;
    char name[64];
    int id;
    int active;
    SOCKET bridge_sock;
    int bridge_port;
} ClientInfo;

ClientInfo clients[MAX_CLIENTS];
CRITICAL_SECTION cs_server;
HWND hServerListBox;

DWORD WINAPI ServerAcceptThread(LPVOID lpParam);
DWORD WINAPI ServerClientHandler(LPVOID lpParam);
DWORD WINAPI ServerBridgeThread(LPVOID lpParam);

// Bridge Thread (Server)
DWORD WINAPI ServerBridgeThread(LPVOID lpParam) {
    int id = (int)(intptr_t)lpParam;
    char buffer[4096];
    int bytesReceived;

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(clients[id].bridge_port);
    
    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        MessageBox(hServerWnd, "Failed to bind bridge port", "Error", MB_OK);
        closesocket(listen_sock);
        return 0;
    }
    listen(listen_sock, 1);
    
    while (clients[id].active) {
        SOCKET app_sock = accept(listen_sock, NULL, NULL);
        if (app_sock == INVALID_SOCKET) continue;
        
        clients[id].bridge_sock = app_sock;
        PostMessage(hServerWnd, WM_UPDATE_LIST, 0, 0);

        while (clients[id].active) {
            bytesReceived = recv(app_sock, buffer, sizeof(buffer), 0);
            if (bytesReceived <= 0) break;
            send(clients[id].sock, buffer, bytesReceived, 0);
        }
        
        closesocket(app_sock);
        clients[id].bridge_sock = INVALID_SOCKET;
        PostMessage(hServerWnd, WM_UPDATE_LIST, 0, 0);
    }
    closesocket(listen_sock);
    return 0;
}

// Client Handler (Server)
DWORD WINAPI ServerClientHandler(LPVOID lpParam) {
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
            PostMessage(hServerWnd, WM_UPDATE_LIST, 0, 0);
        }
    }

    while (clients[id].active) {
        bytesReceived = recv(clients[id].sock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) break;

        if (clients[id].bridge_sock != INVALID_SOCKET) {
            send(clients[id].bridge_sock, buffer, bytesReceived, 0);
        }
    }

    EnterCriticalSection(&cs_server);
    clients[id].active = 0;
    closesocket(clients[id].sock);
    LeaveCriticalSection(&cs_server);
    
    PostMessage(hServerWnd, WM_UPDATE_LIST, 0, 0);
    return 0;
}

// Accept Thread (Server)
DWORD WINAPI ServerAcceptThread(LPVOID lpParam) {
    SOCKET listenSock = (SOCKET)lpParam;
    struct sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);

    while (1) {
        SOCKET clientSock = accept(listenSock, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSock == INVALID_SOCKET) continue;

        EnterCriticalSection(&cs_server);
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
            
            CreateThread(NULL, 0, ServerClientHandler, (LPVOID)(intptr_t)id, 0, NULL);
        } else {
            closesocket(clientSock);
        }
        LeaveCriticalSection(&cs_server);
    }
    return 0;
}

void UpdateServerListBox() {
    SendMessage(hServerListBox, LB_RESETCONTENT, 0, 0);
    EnterCriticalSection(&cs_server);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            char entry[128];
            sprintf(entry, "ID: %d - %s [%s]", i, clients[i].name, 
                    clients[i].bridge_port ? "Mapped" : "Idle");
            int index = SendMessage(hServerListBox, LB_ADDSTRING, 0, (LPARAM)entry);
            SendMessage(hServerListBox, LB_SETITEMDATA, index, (LPARAM)i);
        }
    }
    LeaveCriticalSection(&cs_server);
}

LRESULT CALLBACK ServerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE:
            hServerListBox = CreateWindow("LISTBOX", NULL, 
                WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
                10, 10, 460, 250, hwnd, (HMENU)ID_SERVER_LISTBOX, hInst, NULL);
            SendMessage(hServerListBox, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            HWND hBtn = CreateWindow("BUTTON", "Map Selected to Port...", 
                WS_CHILD | WS_VISIBLE,
                10, 270, 200, 30, hwnd, (HMENU)ID_BTN_MAP, hInst, NULL);
            SendMessage(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Start Server Logic
            InitializeCriticalSection(&cs_server);
            SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            struct sockaddr_in serverAddr;
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_addr.s_addr = INADDR_ANY;
            serverAddr.sin_port = htons(9000);

            if (bind(listenSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
                MessageBox(hwnd, "Could not bind to port 9000", "Error", MB_ICONERROR);
            } else {
                listen(listenSock, 5);
                CreateThread(NULL, 0, ServerAcceptThread, (LPVOID)listenSock, 0, NULL);
            }
            break;
            
        case WM_UPDATE_LIST:
            UpdateServerListBox();
            break;
            
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_MAP) {
                int index = SendMessage(hServerListBox, LB_GETCURSEL, 0, 0);
                if (index != LB_ERR) {
                    int id = SendMessage(hServerListBox, LB_GETITEMDATA, index, 0);
                    int port = 10000 + id;
                    char msg[128];
                    sprintf(msg, "Mapping Client %d to Local Port %d?", id, port);
                    if (MessageBox(hwnd, msg, "Confirm Mapping", MB_YESNO) == IDYES) {
                        EnterCriticalSection(&cs_server);
                        clients[id].bridge_port = port;
                        CreateThread(NULL, 0, ServerBridgeThread, (LPVOID)(intptr_t)id, 0, NULL);
                        LeaveCriticalSection(&cs_server);
                        UpdateServerListBox();
                        
                        sprintf(msg, "Mapped! Connect to localhost:%d", port);
                        MessageBox(hwnd, msg, "Success", MB_OK);
                    }
                }
            }
            break;
            
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            ShowWindow(hLauncherWnd, SW_SHOW);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================================
// CLIENT IMPLEMENTATION
// ============================================================================
// RFC 2217 Definitions
#define IAC  255
#define SB   250
#define SE   240
#define COM_PORT_OPTION 44
#define SET_BAUDRATE 1

HANDLE hClientSerial = INVALID_HANDLE_VALUE;
SOCKET clientSock = INVALID_SOCKET;
int client_running = 0;
HANDLE hClientThread = NULL;

char client_ip[64] = "127.0.0.1";
int client_port = 9000;
char client_com[32] = "COM3";
int client_baud = 115200;

void LogMessage(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    // Append newline if missing
    int len = strlen(buffer);
    if (len > 0 && buffer[len-1] != '\n') {
        strcat(buffer, "\r\n");
    } else {
        // Ensure CRLF for Edit Control
        char* p = strchr(buffer, '\n');
        if (p && (p == buffer || *(p-1) != '\r')) {
            // Simple hack: just replace \n with \r\n replacement logic or just rely on wrapper
            // For simplicity in this demo, just SendMessage directly
        }
    }

    // Send to UI thread
    char* msgCopy = strdup(buffer);
    PostMessage(hClientWnd, WM_LOG_MESSAGE, 0, (LPARAM)msgCopy);
}

void set_baud_rate(int baudRate) {
    if (hClientSerial == INVALID_HANDLE_VALUE) return;

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hClientSerial, &dcbSerialParams)) return;

    dcbSerialParams.BaudRate = baudRate;
    
    if (SetCommState(hClientSerial, &dcbSerialParams)) {
        LogMessage("Baud Rate changed to %d", baudRate);
    } else {
        LogMessage("Failed to set Baud Rate to %d", baudRate);
    }
}

int setup_serial(const char *portName, int baudRate) {
    char adjustedPortName[32];
    sprintf(adjustedPortName, "\\\\.\\%s", portName);

    hClientSerial = CreateFile(adjustedPortName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

    if (hClientSerial == INVALID_HANDLE_VALUE) {
        LogMessage("Error opening serial port %s", portName);
        return 0;
    }

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hClientSerial, &dcbSerialParams)) {
        CloseHandle(hClientSerial);
        return 0;
    }

    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hClientSerial, &dcbSerialParams)) {
        CloseHandle(hClientSerial);
        return 0;
    }

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 10;
    timeouts.ReadTotalTimeoutConstant = 10;
    timeouts.ReadTotalTimeoutMultiplier = 1;
    timeouts.WriteTotalTimeoutConstant = 10;
    timeouts.WriteTotalTimeoutMultiplier = 1;

    SetCommTimeouts(hClientSerial, &timeouts);
    return 1;
}

DWORD WINAPI SerialToTcpThread(LPVOID lpParam) {
    char buffer[4096];
    DWORD bytesRead;

    while (client_running && clientSock != INVALID_SOCKET) {
        if (ReadFile(hClientSerial, buffer, sizeof(buffer), &bytesRead, NULL)) {
            if (bytesRead > 0) {
                if (send(clientSock, buffer, bytesRead, 0) == SOCKET_ERROR) {
                    LogMessage("Server disconnected during send.");
                    client_running = 0;
                    break;
                }
            }
        } else {
            Sleep(10);
        }
    }
    return 0;
}

void process_tcp_data(unsigned char *data, int len) {
    static int state = 0;
    static unsigned char command_buffer[16];
    static int cmd_idx = 0;
    DWORD bytesWritten;
    
    for (int i = 0; i < len; i++) {
        unsigned char c = data[i];

        if (state == 0) {
            if (c == IAC) state = 1;
            else WriteFile(hClientSerial, &c, 1, &bytesWritten, NULL);
        } else if (state == 1) { // Received IAC
            if (c == SB) state = 2;
            else if (c == IAC) {
                WriteFile(hClientSerial, &c, 1, &bytesWritten, NULL);
                state = 0;
            } else state = 0;
        } else if (state == 2) { // Received SB
            if (c == COM_PORT_OPTION) {
                state = 3;
                cmd_idx = 0;
            } else state = 0;
        } else if (state == 3) { // Received COM_PORT_OPTION
            if (c == SET_BAUDRATE) state = 4;
            else {
                if (c == IAC) state = 9;
                else state = 3;
            }
        } else if (state == 4) { // Reading Baud Rate
            if (c == IAC) state = 9;
            else {
                command_buffer[cmd_idx++] = c;
                if (cmd_idx == 4) {
                    uint32_t baud = (command_buffer[0] << 24) | (command_buffer[1] << 16) | 
                                    (command_buffer[2] << 8)  | command_buffer[3];
                    set_baud_rate(baud);
                    state = 9;
                }
            }
        } else if (state == 9) { // Wait for SE
             if (c == SE) state = 0;
        }
    }
}

DWORD WINAPI ClientWorkerThread(LPVOID lpParam) {
    LogMessage("Connecting to Hub %s:%d...", client_ip, client_port);
        
    clientSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(client_port);
    serverAddr.sin_addr.s_addr = inet_addr(client_ip);

    if (connect(clientSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        LogMessage("Connection failed.");
        closesocket(clientSock);
        client_running = 0;
        PostMessage(hClientWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CONNECT, 0), 0); // Reset UI
        return 0;
    }

    LogMessage("Connected to Hub.");

    // Handshake
    char handshake[256];
    snprintf(handshake, sizeof(handshake), "REGISTER:%s\n", client_com);
    send(clientSock, handshake, strlen(handshake), 0);

    // Open Serial
    if (!setup_serial(client_com, client_baud)) {
        LogMessage("Failed to open serial port %s.", client_com);
        closesocket(clientSock);
        client_running = 0;
        PostMessage(hClientWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CONNECT, 0), 0); // Reset UI
        return 0;
    }

    LogMessage("Serial Port %s Opened.", client_com);
    client_running = 1;

    HANDLE hSerialThread = CreateThread(NULL, 0, SerialToTcpThread, NULL, 0, NULL);

    unsigned char buffer[4096];
    int bytesReceived;

    while (client_running) {
        bytesReceived = recv(clientSock, (char*)buffer, sizeof(buffer), 0);
        if (bytesReceived > 0) {
            process_tcp_data(buffer, bytesReceived);
        } else {
            LogMessage("Disconnected from server.");
            break; 
        }
    }

    client_running = 0;
    WaitForSingleObject(hSerialThread, 1000);
    CloseHandle(hSerialThread);
    CloseHandle(hClientSerial);
    hClientSerial = INVALID_HANDLE_VALUE;
    closesocket(clientSock);
    clientSock = INVALID_SOCKET;
    
    LogMessage("Stopped.");
    PostMessage(hClientWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CONNECT, 0), 0); // Reset UI to stopped state
    return 0;
}

LRESULT CALLBACK ClientWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hBtnConnect;
    static HWND hEditIP, hEditPort, hEditCOM, hEditBaud, hLog;

    switch(msg) {
        case WM_CREATE:
            CreateWindow("STATIC", "Hub IP:", WS_CHILD|WS_VISIBLE, 10, 15, 60, 20, hwnd, NULL, hInst, NULL);
            hEditIP = CreateWindow("EDIT", "127.0.0.1", WS_CHILD|WS_VISIBLE|WS_BORDER, 80, 10, 100, 25, hwnd, (HMENU)ID_EDIT_IP, hInst, NULL);
            
            CreateWindow("STATIC", "Port:", WS_CHILD|WS_VISIBLE, 190, 15, 40, 20, hwnd, NULL, hInst, NULL);
            hEditPort = CreateWindow("EDIT", "9000", WS_CHILD|WS_VISIBLE|WS_BORDER, 230, 10, 50, 25, hwnd, (HMENU)ID_EDIT_PORT, hInst, NULL);
            
            CreateWindow("STATIC", "COM:", WS_CHILD|WS_VISIBLE, 10, 45, 60, 20, hwnd, NULL, hInst, NULL);
            hEditCOM = CreateWindow("EDIT", "COM3", WS_CHILD|WS_VISIBLE|WS_BORDER, 80, 40, 80, 25, hwnd, (HMENU)ID_EDIT_COM, hInst, NULL);

            CreateWindow("STATIC", "Baud:", WS_CHILD|WS_VISIBLE, 170, 45, 40, 20, hwnd, NULL, hInst, NULL);
            hEditBaud = CreateWindow("EDIT", "115200", WS_CHILD|WS_VISIBLE|WS_BORDER, 210, 40, 70, 25, hwnd, (HMENU)ID_EDIT_BAUD, hInst, NULL);

            hBtnConnect = CreateWindow("BUTTON", "Connect", WS_CHILD|WS_VISIBLE, 300, 10, 80, 55, hwnd, (HMENU)ID_BTN_CONNECT, hInst, NULL);
            
            hLog = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY, 
                                10, 80, 380, 170, hwnd, (HMENU)ID_LOG_AREA, hInst, NULL);
            
            // Set Fonts
            EnumChildWindows(hwnd, (WNDENUMPROC)SendMessage, (LPARAM)WM_SETFONT); // Simplification, won't work exactly like this but good enough for defaults or manual
            SendMessage(hEditIP, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(hEditPort, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(hEditCOM, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(hEditBaud, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(hBtnConnect, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(hLog, WM_SETFONT, (WPARAM)hFont, TRUE);
            break;

        case WM_LOG_MESSAGE: {
            char* text = (char*)lParam;
            int len = GetWindowTextLength(hLog);
            SendMessage(hLog, EM_SETSEL, len, len);
            SendMessage(hLog, EM_REPLACESEL, 0, (LPARAM)text);
            SendMessage(hLog, EM_REPLACESEL, 0, (LPARAM)"\r\n");
            free(text);
            break;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_CONNECT) {
                // Check if this was a programmatic reset or user click
                if (lParam == 0 && client_running == 0) {
                     SetWindowText(hBtnConnect, "Connect");
                     return 0;
                }

                if (!client_running) {
                    // Start
                    char portStr[10], baudStr[10];
                    GetWindowText(hEditIP, client_ip, sizeof(client_ip));
                    GetWindowText(hEditPort, portStr, sizeof(portStr));
                    GetWindowText(hEditCOM, client_com, sizeof(client_com));
                    GetWindowText(hEditBaud, baudStr, sizeof(baudStr));
                    
                    client_port = atoi(portStr);
                    client_baud = atoi(baudStr);
                    
                    client_running = 1;
                    hClientThread = CreateThread(NULL, 0, ClientWorkerThread, NULL, 0, NULL);
                    SetWindowText(hBtnConnect, "Stop");
                } else {
                    // Stop
                    client_running = 0;
                    closesocket(clientSock); // Force socket close to break recv
                    SetWindowText(hBtnConnect, "Stopping...");
                }
            }
            break;

        case WM_CLOSE:
            if (client_running) {
                client_running = 0;
                closesocket(clientSock);
                if (hClientThread) WaitForSingleObject(hClientThread, 2000);
            }
            ShowWindow(hwnd, SW_HIDE);
            ShowWindow(hLauncherWnd, SW_SHOW);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================================
// LAUNCHER IMPLEMENTATION
// ============================================================================
LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE:
            CreateWindow("STATIC", "Select Mode:", WS_CHILD|WS_VISIBLE|SS_CENTER, 10, 20, 280, 20, hwnd, NULL, hInst, NULL);
            
            HWND hBtnServer = CreateWindow("BUTTON", "Server Mode\n(Hub)", WS_CHILD|WS_VISIBLE|BS_MULTILINE, 
                50, 50, 200, 60, hwnd, (HMENU)ID_BTN_SERVER_MODE, hInst, NULL);
                
            HWND hBtnClient = CreateWindow("BUTTON", "Client Mode\n(Device)", WS_CHILD|WS_VISIBLE|BS_MULTILINE, 
                50, 120, 200, 60, hwnd, (HMENU)ID_BTN_CLIENT_MODE, hInst, NULL);

            SendMessage(hBtnServer, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(hBtnClient, WM_SETFONT, (WPARAM)hFont, TRUE);
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_SERVER_MODE) {
                if (!hServerWnd) {
                    hServerWnd = CreateWindow("HubServerClass", "EasyCOM Server Hub", 
                        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, 
                        CW_USEDEFAULT, CW_USEDEFAULT, 500, 350, 
                        NULL, NULL, hInst, NULL);
                }
                ShowWindow(hwnd, SW_HIDE);
                ShowWindow(hServerWnd, SW_SHOW);
            }
            if (LOWORD(wParam) == ID_BTN_CLIENT_MODE) {
                if (!hClientWnd) {
                    hClientWnd = CreateWindow("PushClientClass", "EasyCOM Client", 
                        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, 
                        CW_USEDEFAULT, CW_USEDEFAULT, 420, 300, 
                        NULL, NULL, hInst, NULL);
                }
                ShowWindow(hwnd, SW_HIDE);
                ShowWindow(hClientWnd, SW_SHOW);
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    hInst = hInstance;
    
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    // Create Font
    hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, 
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    // Register Classes
    WNDCLASS wc = {0};
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hInstance = hInstance;

    // Launcher Class
    wc.lpszClassName = "EasyComLauncher";
    wc.lpfnWndProc = LauncherWndProc;
    RegisterClass(&wc);

    // Server Class
    wc.lpszClassName = "HubServerClass";
    wc.lpfnWndProc = ServerWndProc;
    RegisterClass(&wc);

    // Client Class
    wc.lpszClassName = "PushClientClass";
    wc.lpfnWndProc = ClientWndProc;
    RegisterClass(&wc);

    // Create Launcher Window
    hLauncherWnd = CreateWindow("EasyComLauncher", "EasyCOM Unified", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE, 
        CW_USEDEFAULT, CW_USEDEFAULT, 320, 240, 
        NULL, NULL, hInstance, NULL);

    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    WSACleanup();
    return 0;
}
