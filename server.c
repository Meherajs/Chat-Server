#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9000
#define BUF_SIZE 4096
#define MAX_CLIENTS 10
#define FILE_HDR "FILE:"

typedef struct
{
    SOCKET socket;
    struct sockaddr_in address;
    char username[50];
    // char client_ip[20];
} ClientInfo;
ClientInfo clients[MAX_CLIENTS];
int client_count = 0;
CRITICAL_SECTION cs;

void BroadcastMessage(ClientInfo *sender, const char *message)
{
    char fullmsg[BUF_SIZE + 100];
    snprintf(fullmsg, sizeof(fullmsg), "[%s]: %s", sender->username, message);

    EnterCriticalSection(&cs);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].socket != sender->socket)
        {
            int result = send(clients[i].socket, fullmsg, strlen(fullmsg), 0);
            if (result == SOCKET_ERROR)
            {
                printf("Failed to send message to client %s\n", clients[i].username);
            }
        }
    }
    LeaveCriticalSection(&cs);
}

void BroadcastFileData(ClientInfo *sender, const char *data, int size)
{
    EnterCriticalSection(&cs);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].socket != sender->socket)
        {
            int total_sent = 0;
            while (total_sent < size)
            {
                int sent = send(clients[i].socket, data + total_sent, size - total_sent, 0);
                if (sent == SOCKET_ERROR)
                {
                    printf("Failed to send file data to client %s\n", clients[i].username);
                    break;
                }
                total_sent += sent;
            }
        }
    }
    LeaveCriticalSection(&cs);
}

unsigned __stdcall ClientHandler(void *lpParam)
{
    ClientInfo *client = (ClientInfo *)lpParam;
    char buf[BUF_SIZE];
    int bytes;

    int name_len = recv(client->socket, client->username, sizeof(client->username) - 1, 0);
    if (name_len <= 0)
    {
        printf("Failed to get username from client\n");
        closesocket(client->socket);
        return 0;
    }
    client->username[name_len] = '\0';
    printf("Client connected: %s (%s:%d)\n",
           client->username,
           inet_ntoa(client->address.sin_addr),
           ntohs(client->address.sin_port));

    char join_msg[100];
    snprintf(join_msg, sizeof(join_msg), "*** %s joined the chat ***\n", client->username);
    BroadcastFileData(client, join_msg, strlen(join_msg));

    while (1)
    {
        bytes = recv(client->socket, buf, BUF_SIZE, 0);
        if (bytes <= 0)
        {
            if (bytes == 0)
                printf("Client %s disconnected\n", client->username);
            else
                printf("Client %s recv error: %d\n", client->username, WSAGetLastError());
            break;
        }

        if (bytes > 5 && strncmp(buf, FILE_HDR, 5) == 0)
        {
            char fname[128];
            long fsize;

            char *newline = strchr(buf, '\n');
            if (!newline)
            {
                printf("Invalid file header format\n");
                continue;
            }

            *newline = '\0';
            if (sscanf(buf + 5, "%127[^:]:%ld", fname, &fsize) != 2)
            {
                printf("Failed to parse file header\n");
                *newline = '\n';
                continue;
            }
            *newline = '\n';

            printf("Receiving file '%s' (%ld bytes) from %s\n", fname, fsize, client->username);

            int header_len = (int)(newline - buf) + 1;
            BroadcastFileData(client, buf, header_len);

            int body_bytes = bytes - header_len;
            if (body_bytes > 0)
            {
                BroadcastFileData(client, newline + 1, body_bytes);
            }

            long received = body_bytes;
            while (received < fsize)
            {
                bytes = recv(client->socket, buf, BUF_SIZE, 0);
                if (bytes <= 0)
                {
                    printf("Connection lost while receiving file\n");
                    break;
                }

                BroadcastFileData(client, buf, bytes);
                received += bytes;

                if (fsize > 1024 * 1024)
                {
                    int progress = (int)((received * 100) / fsize);
                    printf("Progress: %d%% (%ld/%ld bytes)\r", progress, received, fsize);
                    fflush(stdout);
                }
            }

            if (received >= fsize)
            {
                printf("\nFile transfer complete: '%s' (%ld bytes)\n", fname, fsize);
            }
            else
            {
                printf("\nFile transfer incomplete: '%s' (%ld/%ld bytes)\n", fname, received, fsize);
            }
        }
        else
        {
            buf[bytes] = '\0';
            printf("[%s]: %s", client->username, buf);
            BroadcastMessage(client, buf);
        }
    }

    char leave_msg[100];
    snprintf(leave_msg, sizeof(leave_msg), "*** %s left the chat ***\n", client->username);
    BroadcastFileData(client, leave_msg, strlen(leave_msg));

    EnterCriticalSection(&cs);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].socket == client->socket)
        {
            closesocket(client->socket);
            memmove(&clients[i], &clients[i + 1],
                    (client_count - i - 1) * sizeof(ClientInfo));
            client_count--;
            break;
        }
    }
    LeaveCriticalSection(&cs);

    return 0;
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    InitializeCriticalSection(&cs);

    WSADATA wsa;
    SOCKET server_sock;
    struct sockaddr_in serv_addr;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed. Error Code: %d\n", WSAGetLastError());
        return 1;
    }

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET)
    {
        printf("Socket creation failed. Error Code: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    int buf_size = 1024 * 1024;
    setsockopt(server_sock, SOL_SOCKET, SO_SNDBUF, (const char *)&buf_size, sizeof(buf_size));
    setsockopt(server_sock, SOL_SOCKET, SO_RCVBUF, (const char *)&buf_size, sizeof(buf_size));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR)
    {
        printf("Bind failed. Error Code: %d\n", WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    if (listen(server_sock, 5) == SOCKET_ERROR)
    {
        printf("Listen failed. Error Code: %d\n", WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    printf("=== Chat Server Started ===\n");
    printf("Server listening on port %d\n", PORT);
    printf("Run 'ipconfig' to find your IP address\n");
    printf("Waiting for connections...\n\n");

    while (1)
    {
        SOCKET client_sock;
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);

        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock == INVALID_SOCKET)
        {
            printf("Accept error: %d\n", WSAGetLastError());
            continue;
        }

        setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, (const char *)&buf_size, sizeof(buf_size));
        setsockopt(client_sock, SOL_SOCKET, SO_RCVBUF, (const char *)&buf_size, sizeof(buf_size));

        EnterCriticalSection(&cs);
        if (client_count < MAX_CLIENTS)
        {
            ClientInfo *client = (ClientInfo *)malloc(sizeof(ClientInfo));
            if (client)
            {
                client->socket = client_sock;
                client->address = client_addr;
                client->username[0] = '\0';

                clients[client_count] = *client;
                client_count++;

                HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, ClientHandler, client, 0, NULL);
                if (thread == NULL)
                {
                    printf("Thread creation failed\n");
                    closesocket(client_sock);
                    free(client);
                    client_count--;
                }
                else
                {
                    CloseHandle(thread);
                    printf("New client connected (%s:%d). Total clients: %d\n",
                           inet_ntoa(client_addr.sin_addr),
                           ntohs(client_addr.sin_port),
                           client_count);
                }
            }
        }
        else
        {
            const char *msg = "Server full. Try again later.\n";
            send(client_sock, msg, strlen(msg), 0);
            closesocket(client_sock);
            printf("Rejected client (max connections reached)\n");
        }
        LeaveCriticalSection(&cs);
    }

    closesocket(server_sock);
    WSACleanup();
    DeleteCriticalSection(&cs);
    return 0;
}

