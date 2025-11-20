// chat_client.c
// 客户端程序 —— 与现有 NKU Chat Server 完整对接（优化版）
//
// 特性：
//  - 启动欢迎界面（Features / Basic Commands / Message Sending）
//  - 支持 /list /quit /exit /help 命令
//  - 支持英文和中文消息，自动显示时间戳与用户名
//  - 使用独立接收线程显示服务器广播消息

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include "chat_protocol.h"   // 已包含 winsock2.h 等

#define MAX_INPUT_LEN 2048

static SOCKET client_socket = INVALID_SOCKET;
static char   g_username[MAX_USERNAME_LEN] = {0};
static volatile int g_running = 1;

/*=============================
 *  辅助输出函数
 *=============================*/

void print_banner(void) {
    printf("============================================================\n");
    printf("      Welcome to Multi-User Chat Room (TCP Socket)\n");
    printf("============================================================\n\n");

    printf("[Features]\n");
    printf("  - Multi-user chat room based on TCP streaming sockets\n");
    printf("  - Supports English and Chinese messages with timestamps\n");
    printf("  - Each user is automatically assigned a unique ID\n");
    printf("  - You can set your own nickname (must be unique)\n\n");

    printf("[Basic Commands]\n");
    printf("  /list  - View online users list (shows ID and nickname)\n");
    printf("  /quit  - Exit chat room\n");
    printf("  /exit  - Exit chat room (same as /quit)\n");
    printf("  /help  - Show this help message\n\n");

    printf("[Message Sending]\n");
    printf("  - Type text directly to send messages (supports English/Chinese)\n");
    printf("  - Messages are automatically broadcast to all online users\n");
    printf("  - Each message displays timestamp and username\n\n");
    printf("============================================================\n\n");
}

void print_help(void) {
    printf("\n[Command Help]\n");
    printf("  /list  - View online users list (shows ID and nickname)\n");
    printf("  /quit  - Exit chat room\n");
    printf("  /exit  - Exit chat room (same as /quit)\n");
    printf("  /help  - Show this help message\n\n");
}

/*=============================
 *  协议发送封装
 *=============================*/

int send_chat_message(const ChatMessage *msg) {
    char buffer[MAX_BUFFER_SIZE];
    int len = serialize_message(msg, buffer, sizeof(buffer));
    if (len < 0) {
        printf("Failed to serialize message.\n");
        return -1;
    }
    if (send(client_socket, buffer, len, 0) == SOCKET_ERROR) {
        printf("send failed: %d\n", WSAGetLastError());
        return -1;
    }
    if (send(client_socket, "\n", 1, 0) == SOCKET_ERROR) {
        printf("send newline failed: %d\n", WSAGetLastError());
        return -1;
    }
    return 0;
}

/*=============================
 *  接收线程：负责显示服务器推送
 *=============================*/

DWORD WINAPI recv_thread(LPVOID lpParam) {
    (void)lpParam;

    char buffer[MAX_BUFFER_SIZE];
    char recv_buffer[MAX_BUFFER_SIZE * 2];
    int recv_pos = 0;
    ChatMessage msg;

    while (g_running) {
        int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            if (g_running) {
                printf("\n[CLIENT] Connection closed or recv failed (bytes=%d)\n", bytes);
            }
            g_running = 0;
            break;
        }

        buffer[bytes] = '\0';

        if (recv_pos + bytes < (int)sizeof(recv_buffer) - 1) {
            memcpy(recv_buffer + recv_pos, buffer, bytes);
            recv_pos += bytes;
            recv_buffer[recv_pos] = '\0';
        }

        char *line_start = recv_buffer;
        char *line_end   = NULL;

        while ((line_end = strchr(line_start, '\n')) != NULL) {
            *line_end = '\0';

            if (deserialize_message(line_start, &msg) == 0) {
                switch (msg.type) {
                case MSG_MESSAGE:
                case MSG_SYSTEM:
                case MSG_ACK:
                case MSG_ERROR:
                    printf("\n[%s] %s: %s\n",
                           msg.timestamp, msg.username, msg.content);
                    break;
                default:
                    break;
                }
            }

            line_start = line_end + 1;
        }

        if (line_start > recv_buffer) {
            int remaining = recv_pos - (int)(line_start - recv_buffer);
            memmove(recv_buffer, line_start, remaining);
            recv_pos = remaining;
            recv_buffer[recv_pos] = '\0';
        }
    }

    return 0;
}

/*=============================
 *  发送昵称注册消息
 *=============================*/

int send_nickname(const char *nickname) {
    ChatMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.type = MSG_NICKNAME;
    get_timestamp(msg.timestamp, sizeof(msg.timestamp));
    strncpy(msg.username, "CLIENT", MAX_USERNAME_LEN - 1);

    strncpy(msg.content, nickname, MAX_MESSAGE_LEN - 1);
    msg.content_length = (int)strlen(msg.content);

    return send_chat_message(&msg);
}

/*=============================
 *  主函数
 *=============================*/

int main(int argc, char *argv[]) {
    WSADATA wsaData;
    char server_ip[64] = "127.0.0.1";
    int  server_port = SERVER_PORT;
    char input[MAX_INPUT_LEN];

    print_banner();

    /* 1. 服务器 IP 输入（可命令行传参） */
    if (argc >= 2) {
        strncpy(server_ip, argv[1], sizeof(server_ip) - 1);
        server_ip[sizeof(server_ip) - 1] = '\0';
    } else {
        printf("Connecting to server %s:%d...\n", server_ip, server_port);
    }

    /* 2. 输入昵称 */
    while (1) {
        printf("\nPlease enter your nickname (1-%d characters, must be unique): ",
               MAX_USERNAME_LEN - 1);
        if (!fgets(input, sizeof(input), stdin)) {
            return 0;
        }
        input[strcspn(input, "\r\n")] = '\0';

        if (strlen(input) == 0) {
            printf("Nickname cannot be empty. Please try again.\n");
            continue;
        }
        if (strchr(input, '|') != NULL) {
            printf("Nickname must not contain '|'. Please try again.\n");
            continue;
        }
        strncpy(g_username, input, sizeof(g_username) - 1);
        g_username[sizeof(g_username) - 1] = '\0';
        break;
    }

    /* 3. 初始化 WinSock */
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    /* 4. 创建 socket 并连接服务器 */
    client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    printf("\nConnecting to server %s:%d...\n", server_ip, server_port);
    if (connect(client_socket, (struct sockaddr*)&server_addr,
                sizeof(server_addr)) == SOCKET_ERROR) {
        printf("connect failed: %d\n", WSAGetLastError());
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }
    printf("Connected successfully!\n\n");

    /* 5. 发送昵称并等待 ACK / ERROR */
    printf("Sending nickname to server...\n");
    if (send_nickname(g_username) != 0) {
        printf("Failed to send nickname.\n");
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }
    printf("Nickname sent successfully. Waiting for server response...\n");

    {
        char buffer[MAX_BUFFER_SIZE];
        char recv_buf[MAX_BUFFER_SIZE * 2];
        int recv_pos = 0;
        int got_first = 0;
        ChatMessage msg;

        while (!got_first) {
            int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) {
                printf("Server closed connection or recv failed (bytes=%d)\n", bytes);
                closesocket(client_socket);
                WSACleanup();
                return 1;
            }

            buffer[bytes] = '\0';
            if (recv_pos + bytes < (int)sizeof(recv_buf) - 1) {
                memcpy(recv_buf + recv_pos, buffer, bytes);
                recv_pos += bytes;
                recv_buf[recv_pos] = '\0';
            }

            char *line_start = recv_buf;
            char *line_end   = NULL;

            while ((line_end = strchr(line_start, '\n')) != NULL) {
                *line_end = '\0';

                if (deserialize_message(line_start, &msg) == 0) {
                    if (msg.type == MSG_ACK) {
                        printf("[Server] %s\n", msg.content);
                        got_first = 1;
                    } else if (msg.type == MSG_ERROR) {
                        printf("[Server Error] %s\n", msg.content);
                        closesocket(client_socket);
                        WSACleanup();
                        return 1;
                    } else {
                        printf("[%s] %s: %s\n",
                               msg.timestamp, msg.username, msg.content);
                    }
                }

                line_start = line_end + 1;
            }

            if (line_start > recv_buf) {
                int remaining = recv_pos - (int)(line_start - recv_buf);
                memmove(recv_buf, line_start, remaining);
                recv_pos = remaining;
                recv_buf[recv_pos] = '\0';
            }

            if (got_first) break;
        }
    }

    /* 6. 启动接收线程 */
    HANDLE hThread = CreateThread(NULL, 0, recv_thread, NULL, 0, NULL);
    if (hThread == NULL) {
        printf("Failed to create recv thread.\n");
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }
    CloseHandle(hThread);

    /* 7. 主循环：读取用户输入并发送消息/命令 */
    printf("\nStart chatting (type message or use commands, type /help for help):\n");
    while (g_running) {
        printf("> ");
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        input[strcspn(input, "\r\n")] = '\0';
        if (strlen(input) == 0) {
            continue;
        }

        /* 命令处理 */
        if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
            ChatMessage msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = MSG_LEAVE;
            get_timestamp(msg.timestamp, sizeof(msg.timestamp));
            strncpy(msg.username, g_username, MAX_USERNAME_LEN - 1);
            msg.content[0] = '\0';
            msg.content_length = 0;
            send_chat_message(&msg);
            g_running = 0;
            break;
        } else if (strcmp(input, "/list") == 0) {
            ChatMessage msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = MSG_LIST;
            get_timestamp(msg.timestamp, sizeof(msg.timestamp));
            strncpy(msg.username, g_username, MAX_USERNAME_LEN - 1);
            msg.content[0] = '\0';
            msg.content_length = 0;
            send_chat_message(&msg);
        } else if (strcmp(input, "/help") == 0) {
            print_help();
        } else {
            /* 普通聊天消息 */
            ChatMessage msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = MSG_MESSAGE;
            get_timestamp(msg.timestamp, sizeof(msg.timestamp));
            strncpy(msg.username, g_username, MAX_USERNAME_LEN - 1);
            strncpy(msg.content, input, MAX_MESSAGE_LEN - 1);
            msg.content_length = (int)strlen(msg.content);
            send_chat_message(&msg);
        }
    }

    printf("\nDisconnected. Goodbye!\n");
    closesocket(client_socket);
    WSACleanup();
    return 0;
}