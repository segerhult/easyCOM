#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

static const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\VirtualComPipe";

typedef struct {
    HANDLE pipe;
    SOCKET sock;
} BridgeCtx;

static SOCKET connect_tcp(const char* host, int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return INVALID_SOCKET;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr(host);
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

static HANDLE create_pipe_server() {
    HANDLE h = CreateNamedPipeW(
        PIPE_NAME,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,
        4096,
        4096,
        0,
        NULL
    );
    return h;
}

static DWORD WINAPI pipe_to_tcp(LPVOID param) {
    BridgeCtx* ctx = (BridgeCtx*)param;
    char buf[4096];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(ctx->pipe, buf, sizeof(buf), &read, NULL)) break;
        if (read == 0) continue;
        int sent = 0;
        while (sent < (int)read) {
            int r = send(ctx->sock, buf + sent, (int)read - sent, 0);
            if (r <= 0) goto end;
            sent += r;
        }
    }
end:
    return 0;
}

static DWORD WINAPI tcp_to_pipe(LPVOID param) {
    BridgeCtx* ctx = (BridgeCtx*)param;
    char buf[4096];
    for (;;) {
        int r = recv(ctx->sock, buf, sizeof(buf), 0);
        if (r <= 0) break;
        DWORD written = 0;
        if (!WriteFile(ctx->pipe, buf, (DWORD)r, &written, NULL)) break;
    }
    return 0;
}

int wmain(int argc, wchar_t** wargv) {
    char host[64] = "127.0.0.1";
    int port = 10000;
    if (argc >= 2) {
        int len = WideCharToMultiByte(CP_ACP, 0, wargv[1], -1, host, sizeof(host), NULL, NULL);
        if (len <= 0) return 1;
    }
    if (argc >= 3) {
        port = _wtoi(wargv[2]);
        if (port <= 0) port = 10000;
    }

    HANDLE pipe = create_pipe_server();
    if (pipe == INVALID_HANDLE_VALUE) return 1;
    BOOL ok = ConnectNamedPipe(pipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (!ok) {
        CloseHandle(pipe);
        return 1;
    }

    SOCKET s = connect_tcp(host, port);
    if (s == INVALID_SOCKET) {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        return 1;
    }

    BridgeCtx ctx;
    ctx.pipe = pipe;
    ctx.sock = s;

    HANDLE t1 = CreateThread(NULL, 0, pipe_to_tcp, &ctx, 0, NULL);
    HANDLE t2 = CreateThread(NULL, 0, tcp_to_pipe, &ctx, 0, NULL);
    HANDLE th[2] = { t1, t2 };
    WaitForMultipleObjects(2, th, TRUE, INFINITE);

    closesocket(s);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    WSACleanup();
    return 0;
}
