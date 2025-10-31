#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_PORT 9000
#define BUF_SIZE 4096
#define FILE_HDR "FILE:"

void create_download_dir()
{
    CreateDirectoryA("downloads", NULL);
}

void send_file(SOCKET sock, const char *path)
{
    // Check if file exists
    int fd = _open(path, _O_RDONLY | _O_BINARY);
    if (fd < 0)
    {
        printf("Error: Cannot open file '%s'\n", path);
        printf("Make sure the file exists and you have read permissions\n");
        return;
    }

    struct _stat st;
    if (_fstat(fd, &st) != 0)
    {
        printf("Error: Cannot get file stats for '%s'\n", path);
        _close(fd);
        return;
    }

    long filesize = st.st_size;
    if (filesize == 0)
    {
        printf("Error: File '%s' is empty\n", path);
        _close(fd);
        return;
    }

    const char *filename = strrchr(path, '\\');
    if (!filename)
        filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;

    printf("Sending file '%s' (%ld bytes)...\n", filename, filesize);

    char header[256];
    snprintf(header, sizeof(header), "FILE:%s:%ld\n", filename, filesize);
    int header_sent = send(sock, header, strlen(header), 0);
    if (header_sent == SOCKET_ERROR)
    {
        printf("Error: Failed to send file header. Error: %d\n", WSAGetLastError());
        _close(fd);
        return;
    }

    char buf[BUF_SIZE];
    long sent = 0;
    int last_progress = -1;

    while (sent < filesize)
    {
        int r = _read(fd, buf, BUF_SIZE);
        if (r <= 0)
        {
            printf("Error: Failed to read from file\n");
            break;
        }

        int total_sent = 0;
        while (total_sent < r)
        {
            int result = send(sock, buf + total_sent, r - total_sent, 0);
            if (result == SOCKET_ERROR)
            {
                printf("Error: Failed to send file data. Error: %d\n", WSAGetLastError());
                _close(fd);
                return;
            }
            total_sent += result;
        }

        sent += r;

        if (filesize > 1024 * 1024) // > 1MB
        {
            int progress = (int)((sent * 100) / filesize);
            if (progress != last_progress && progress % 10 == 0)
            {
                printf("Progress: %d%% (%ld/%ld bytes)\n", progress, sent, filesize);
                last_progress = progress;
            }
        }

        Sleep(1);
    }

    _close(fd);

    if (sent == filesize)
    {
        printf("File sent successfully: '%s' (%ld bytes)\n", filename, filesize);
    }
    else
    {
        printf("File transfer incomplete: '%s' (%ld/%ld bytes)\n", filename, sent, filesize);
    }
}
int main(int argc, char *argv[])
{
    char server_ip[100] = "";
    int server_port = DEFAULT_PORT;

    printf("Enter server IP : ");

    fgets(server_ip, sizeof(server_ip), stdin);

    
    server_ip[strcspn(server_ip, "\r\n")] = 0;

    if (argc > 2)
    {
        server_port = atoi(argv[2]);
        if (server_port <= 0 || server_port > 65535)
        {
            printf("Invalid port number. Using default port %d\n", DEFAULT_PORT);
            server_port = DEFAULT_PORT;
        }
    }

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    create_download_dir();

    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in serv_addr;
    char buf[BUF_SIZE];

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed. Error Code: %d\n", WSAGetLastError());
        return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
    {
        printf("Socket creation failed. Error Code: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    int buf_size = 1024 * 1024; // 1MB buffer
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char *)&buf_size, sizeof(buf_size));
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *)&buf_size, sizeof(buf_size));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0)
    {
        printf("Invalid address: %s\n", server_ip);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Connecting to %s:%d...\n", server_ip, server_port);
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("Connection failed: %d\n", WSAGetLastError());
        printf("Make sure:\n");
        printf("1. Server is running\n");
        printf("2. IP address is correct\n");
        printf("3. Port %d is not blocked by firewall\n", server_port);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Connected to %s:%d!\n", server_ip, server_port);
    char username[50];
    printf("Enter your username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\r\n")] = 0;

    printf("\nCommands:\n");
    printf("  /send <file>  - Send a file\n");
    printf("  /exit         - Quit the program\n");
    printf("Received files are saved in 'downloads' folder\n");
    printf("========================================\n\n");

    send(sock, username, strlen(username), 0);

    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    int running = 1;

    FILE *current_file = NULL;
    long remaining_bytes = 0;
    char current_filename[128] = {0};

    while (running)
    {
        if (WaitForSingleObject(hStdin, 100) == WAIT_OBJECT_0)
        {
            if (!fgets(buf, BUF_SIZE, stdin))
                break;
            buf[strcspn(buf, "\r\n")] = 0;

            if (strcmp(buf, "/exit") == 0)
            {
                running = 0;
            }
            else if (strncmp(buf, "/send ", 6) == 0)
            {
                send_file(sock, buf + 6);
            }
            else if (strlen(buf) > 0)
            {
                strcat(buf, "\n");
                send(sock, buf, strlen(buf), 0);
            }
        }

        int bytes = recv(sock, buf, BUF_SIZE, 0);
        if (bytes > 0)
        {
            if (current_file)
            {
                fwrite(buf, 1, bytes, current_file);
                remaining_bytes -= bytes;
                if (remaining_bytes <= 0)
                {
                    fclose(current_file);
                    current_file = NULL;
                    printf("\nReceived file: downloads\\%s\n", current_filename);
                    fflush(stdout);
                }
            }
            else if (bytes > 5 && strncmp(buf, FILE_HDR, 5) == 0)
            {
                char *data_start = strchr(buf, '\n');
                if (data_start)
                {
                    data_start++;
                    sscanf(buf + 5, "%127[^:]:%ld", current_filename, &remaining_bytes);

                    char path[MAX_PATH];
                    snprintf(path, sizeof(path), "downloads\\%s", current_filename);
                    current_file = fopen(path, "wb");
                    if (!current_file)
                    {
                        perror("File creation failed");
                        current_file = NULL;
                        remaining_bytes = 0;
                        continue;
                    }

                    int header_len = (int)(data_start - buf);
                    int body_bytes = bytes - header_len;
                    if (body_bytes > 0)
                    {
                        fwrite(data_start, 1, body_bytes, current_file);
                        remaining_bytes -= body_bytes;
                    }

                    printf("\nReceiving file: %s (%ld bytes)\n", current_filename, remaining_bytes + body_bytes);
                    fflush(stdout);
                }
            }
            else
            {
                buf[bytes] = '\0';
                printf("%s", buf);
                fflush(stdout);
            }
        }
        else if (bytes == 0)
        {
            printf("\nDisconnected from server\n");
            break;
        }
        else if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            printf("\nrecv failed: %d\n", WSAGetLastError());
            break;
        }
    }

    if (current_file)
    {
        fclose(current_file);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
