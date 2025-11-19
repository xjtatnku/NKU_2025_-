#include "chat_protocol.h"

// Client information structure
typedef struct {
    SOCKET socket;
    int user_id;                    // User ID assigned by server
    char username[MAX_USERNAME_LEN]; // Username (nickname)
    int active;
} ClientInfo;

// Global variables
static ClientInfo clients[MAX_CLIENTS];
static int client_count = 0;
static int next_user_id = 1;         // Next user ID to assign
static HANDLE client_mutex = NULL;
static SOCKET server_socket = INVALID_SOCKET;
static int server_running = 1;

/**
 * Initialize server socket
 */
int init_server() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        printf("WSAStartup failed: %d\n", result);
        return -1;
    }
    
    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        printf("Socket creation failed: %ld\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed: %ld\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return -1;
    }
    
    // Listen
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        printf("Listen failed: %ld\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return -1;
    }
    
    printf("==============================================================\n");
    printf("           NKU Chat Server\n");
    printf("==============================================================\n");
    printf("Server started on port %d\n", SERVER_PORT);
    printf("Waiting for clients...\n");
    
    return 0;
}

/**
 * Add client to list (thread-safe)
 * Returns user_id on success, negative on error
 */
int add_client(SOCKET socket, const char *username, int *assigned_id) {
    WaitForSingleObject(client_mutex, INFINITE);
    
    if (client_count >= MAX_CLIENTS) {
        ReleaseMutex(client_mutex);
        return -1;
    }
    
    // Check for duplicate username
    for (int i = 0; i < client_count; i++) {
        if (clients[i].active && strcmp(clients[i].username, username) == 0) {
            ReleaseMutex(client_mutex);
            return -2; // Duplicate username
        }
    }
    
    // Assign user ID and add new client
    int user_id = next_user_id++;
    clients[client_count].socket = socket;
    clients[client_count].user_id = user_id;
    strncpy(clients[client_count].username, username, MAX_USERNAME_LEN - 1);
    clients[client_count].username[MAX_USERNAME_LEN - 1] = '\0';
    clients[client_count].active = 1;
    client_count++;
    
    if (assigned_id != NULL) {
        *assigned_id = user_id;
    }
    
    ReleaseMutex(client_mutex);
    return 0;
}

/**
 * Remove client from list (thread-safe)
 */
void remove_client(SOCKET socket) {
    WaitForSingleObject(client_mutex, INFINITE);
    
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket == socket && clients[i].active) {
            clients[i].active = 0;
            closesocket(clients[i].socket);
            break;
        }
    }
    
    ReleaseMutex(client_mutex);
}

/**
 * Broadcast message to all active clients except sender
 */
void broadcast_message(const ChatMessage *msg, SOCKET sender_socket) {
    char buffer[MAX_BUFFER_SIZE];
    int len = serialize_message(msg, buffer, sizeof(buffer));
    if (len < 0) return;
    
    WaitForSingleObject(client_mutex, INFINITE);
    
    for (int i = 0; i < client_count; i++) {
        if (clients[i].active && clients[i].socket != sender_socket) {
            send(clients[i].socket, buffer, len, 0);
            send(clients[i].socket, "\n", 1, 0); // Add newline for easier parsing
        }
    }
    
    ReleaseMutex(client_mutex);
}

/**
 * Send message to specific client
 */
void send_to_client(SOCKET socket, const ChatMessage *msg) {
    char buffer[MAX_BUFFER_SIZE];
    int len = serialize_message(msg, buffer, sizeof(buffer));
    if (len < 0) return;
    
    send(socket, buffer, len, 0);
    send(socket, "\n", 1, 0);
}

/**
 * Get list of online users as string (with ID and nickname)
 */
void get_user_list(char *buffer, size_t buffer_size) {
    WaitForSingleObject(client_mutex, INFINITE);
    
    buffer[0] = '\0';
    int first = 1;
    char temp[128];
    
    for (int i = 0; i < client_count; i++) {
        if (clients[i].active) {
            if (!first) {
                strncat(buffer, ", ", buffer_size - strlen(buffer) - 1);
            }
            snprintf(temp, sizeof(temp), "[ID:%d]%s", clients[i].user_id, clients[i].username);
            strncat(buffer, temp, buffer_size - strlen(buffer) - 1);
            first = 0;
        }
    }
    
    ReleaseMutex(client_mutex);
}

/**
 * Validate username
 */
int validate_username(const char *username) {
    if (username == NULL) return 0;
    
    size_t len = strlen(username);
    if (len == 0 || len >= MAX_USERNAME_LEN) return 0;
    
    // Check for pipe character (not allowed)
    if (strchr(username, '|') != NULL) return 0;
    
    return 1;
}

/**
 * Client handler thread function
 */
DWORD WINAPI client_handler(LPVOID lpParam) {
    SOCKET client_socket = (SOCKET)(UINT_PTR)lpParam;
    char buffer[MAX_BUFFER_SIZE];
    char recv_buffer[MAX_BUFFER_SIZE * 2];
    int recv_pos = 0;
    ChatMessage msg;
    int username_set = 0;
    char client_username[MAX_USERNAME_LEN];
    
    // Wait for NICKNAME message (with timeout)
    printf("Waiting for NICKNAME message from client...\n");
    fd_set readfds;
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    
    FD_ZERO(&readfds);
    FD_SET(client_socket, &readfds);
    
    int select_result = select(0, &readfds, NULL, NULL, &timeout);
    if (select_result <= 0 || !FD_ISSET(client_socket, &readfds)) {
        printf("Client connection timeout (select_result=%d)\n", select_result);
        closesocket(client_socket);
        return 1;
    }
    
    printf("Data available, receiving NICKNAME message...\n");
    // Receive initial NICKNAME message
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        printf("recv failed or connection closed (bytes=%d)\n", bytes_received);
        closesocket(client_socket);
        return 1;
    }
    
    buffer[bytes_received] = '\0';
    printf("Received %d bytes: %s\n", bytes_received, buffer);
    
    // Remove newline if present
    char *newline_pos = strchr(buffer, '\n');
    if (newline_pos != NULL) {
        *newline_pos = '\0';
    }
    
    // Parse NICKNAME message
    if (deserialize_message(buffer, &msg) == 0 && msg.type == MSG_NICKNAME) {
        printf("Successfully parsed NICKNAME message, nickname: %s\n", msg.content);
        if (!validate_username(msg.content)) {
            // Send error
            ChatMessage error_msg;
            error_msg.type = MSG_ERROR;
            get_timestamp(error_msg.timestamp, sizeof(error_msg.timestamp));
            strncpy(error_msg.username, "SERVER", MAX_USERNAME_LEN - 1);
            strncpy(error_msg.content, "Invalid nickname format", MAX_MESSAGE_LEN - 1);
            error_msg.content_length = strlen(error_msg.content);
            send_to_client(client_socket, &error_msg);
            closesocket(client_socket);
            return 1;
        }
        
        // Try to add client with nickname, get assigned user ID
        int assigned_id = 0;
        int result = add_client(client_socket, msg.content, &assigned_id);
        if (result == -2) {
            // Duplicate nickname
            ChatMessage error_msg;
            error_msg.type = MSG_ERROR;
            get_timestamp(error_msg.timestamp, sizeof(error_msg.timestamp));
            strncpy(error_msg.username, "SERVER", MAX_USERNAME_LEN - 1);
            strncpy(error_msg.content, "Nickname already exists, please choose another one", MAX_MESSAGE_LEN - 1);
            error_msg.content_length = strlen(error_msg.content);
            send_to_client(client_socket, &error_msg);
            closesocket(client_socket);
            return 1;
        } else if (result != 0) {
            // Server full
            ChatMessage error_msg;
            error_msg.type = MSG_ERROR;
            get_timestamp(error_msg.timestamp, sizeof(error_msg.timestamp));
            strncpy(error_msg.username, "SERVER", MAX_USERNAME_LEN - 1);
            strncpy(error_msg.content, "Server is full", MAX_MESSAGE_LEN - 1);
            error_msg.content_length = strlen(error_msg.content);
            send_to_client(client_socket, &error_msg);
            closesocket(client_socket);
            return 1;
        }
        
        username_set = 1;
        strncpy(client_username, msg.content, MAX_USERNAME_LEN - 1);
        client_username[MAX_USERNAME_LEN - 1] = '\0';
        
        // Send ACK with assigned user ID
        ChatMessage ack_msg;
        ack_msg.type = MSG_ACK;
        get_timestamp(ack_msg.timestamp, sizeof(ack_msg.timestamp));
        strncpy(ack_msg.username, "SERVER", MAX_USERNAME_LEN - 1);
        snprintf(ack_msg.content, MAX_MESSAGE_LEN, "Joined successfully! Your user ID is: %d, nickname: %s", assigned_id, msg.content);
        ack_msg.content_length = strlen(ack_msg.content);
        send_to_client(client_socket, &ack_msg);
        
        // Broadcast system message
        ChatMessage system_msg;
        system_msg.type = MSG_SYSTEM;
        get_timestamp(system_msg.timestamp, sizeof(system_msg.timestamp));
        strncpy(system_msg.username, "SERVER", MAX_USERNAME_LEN - 1);
        snprintf(system_msg.content, MAX_MESSAGE_LEN, "User [ID:%d]%s has joined the chat room", assigned_id, msg.content);
        system_msg.content_length = strlen(system_msg.content);
        broadcast_message(&system_msg, client_socket);
        
        printf("User [ID:%d]%s joined\n", assigned_id, msg.content);
    } else {
        printf("Failed to parse NICKNAME message or wrong message type\n");
        printf("Buffer content: %s\n", buffer);
        closesocket(client_socket);
        return 1;
    }
    
    // Main message loop
    while (server_running && username_set) {
        FD_ZERO(&readfds);
        FD_SET(client_socket, &readfds);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        if (select(0, &readfds, NULL, NULL, &timeout) > 0 && FD_ISSET(client_socket, &readfds)) {
            bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
            
            if (bytes_received <= 0) {
                // Client disconnected
                break;
            }
            
            buffer[bytes_received] = '\0';
            
            // Append to recv buffer
            if (recv_pos + bytes_received < sizeof(recv_buffer) - 1) {
                memcpy(recv_buffer + recv_pos, buffer, bytes_received);
                recv_pos += bytes_received;
                recv_buffer[recv_pos] = '\0';
            }
            
            // Process complete messages (lines)
            char *line_start = recv_buffer;
            char *line_end;
            while ((line_end = strchr(line_start, '\n')) != NULL) {
                *line_end = '\0';
                
                if (deserialize_message(line_start, &msg) == 0) {
                    switch (msg.type) {
                        case MSG_MESSAGE:
                            // Broadcast message to all clients
                            broadcast_message(&msg, client_socket);
                            break;
                            
                        case MSG_LIST:
                            // Send user list
                            {
                                ChatMessage list_msg;
                                list_msg.type = MSG_MESSAGE;
                                get_timestamp(list_msg.timestamp, sizeof(list_msg.timestamp));
                                strncpy(list_msg.username, "SERVER", MAX_USERNAME_LEN - 1);
                                char user_list[MAX_MESSAGE_LEN];
                                get_user_list(user_list, sizeof(user_list));
                                snprintf(list_msg.content, MAX_MESSAGE_LEN, "Online users: %s", user_list);
                                list_msg.content_length = strlen(list_msg.content);
                                send_to_client(client_socket, &list_msg);
                            }
                            break;
                            
                        case MSG_LEAVE:
                            // Remove client and broadcast
                            {
                                // Find user ID before removing
                                int user_id = 0;
                                WaitForSingleObject(client_mutex, INFINITE);
                                for (int i = 0; i < client_count; i++) {
                                    if (clients[i].socket == client_socket && clients[i].active) {
                                        user_id = clients[i].user_id;
                                        break;
                                    }
                                }
                                ReleaseMutex(client_mutex);
                                
                                ChatMessage system_msg;
                                system_msg.type = MSG_SYSTEM;
                                get_timestamp(system_msg.timestamp, sizeof(system_msg.timestamp));
                                strncpy(system_msg.username, "SERVER", MAX_USERNAME_LEN - 1);
                                snprintf(system_msg.content, MAX_MESSAGE_LEN, "User [ID:%d]%s has left the chat room", user_id, client_username);
                                system_msg.content_length = strlen(system_msg.content);
                                remove_client(client_socket);
                                broadcast_message(&system_msg, client_socket);
                                printf("User [ID:%d]%s left\n", user_id, client_username);
                                closesocket(client_socket);
                                return 0;
                            }
                            break;
                            
                        default:
                            break;
                    }
                }
                
                line_start = line_end + 1;
            }
            
            // Move remaining data to beginning of buffer
            if (line_start > recv_buffer) {
                int remaining = recv_pos - (line_start - recv_buffer);
                memmove(recv_buffer, line_start, remaining);
                recv_pos = remaining;
            }
        }
    }
    
    // Client disconnected unexpectedly
    if (username_set) {
        // Find user ID before removing
        int user_id = 0;
        WaitForSingleObject(client_mutex, INFINITE);
        for (int i = 0; i < client_count; i++) {
            if (clients[i].socket == client_socket && clients[i].active) {
                user_id = clients[i].user_id;
                break;
            }
        }
        ReleaseMutex(client_mutex);
        
        ChatMessage system_msg;
        system_msg.type = MSG_SYSTEM;
        get_timestamp(system_msg.timestamp, sizeof(system_msg.timestamp));
        strncpy(system_msg.username, "SERVER", MAX_USERNAME_LEN - 1);
        snprintf(system_msg.content, MAX_MESSAGE_LEN, "User [ID:%d]%s has disconnected", user_id, client_username);
        system_msg.content_length = strlen(system_msg.content);
        remove_client(client_socket);
        broadcast_message(&system_msg, client_socket);
        printf("User [ID:%d]%s disconnected\n", user_id, client_username);
    } else {
        closesocket(client_socket);
    }
    
    return 0;
}

/**
 * Main server function
 */
int main() {
    printf("=== NKU Chat Room Server ===\n");
    
    // Create mutex for thread synchronization
    client_mutex = CreateMutex(NULL, FALSE, NULL);
    if (client_mutex == NULL) {
        printf("Failed to create mutex\n");
        return 1;
    }
    
    // Initialize server
    if (init_server() != 0) {
        CloseHandle(client_mutex);
        return 1;
    }
    
    // Initialize client list
    memset(clients, 0, sizeof(clients));
    
    // Main accept loop
    while (server_running) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_socket == INVALID_SOCKET) {
            if (server_running) {
                printf("Accept failed: %ld\n", WSAGetLastError());
            }
            continue;
        }
        
        printf("New connection from %s:%d\n", 
            inet_ntoa(client_addr.sin_addr), 
            ntohs(client_addr.sin_port));
        
        // Create thread for client
        HANDLE thread = CreateThread(NULL, 0, client_handler, (LPVOID)(UINT_PTR)client_socket, 0, NULL);
        if (thread == NULL) {
            printf("Failed to create thread\n");
            closesocket(client_socket);
        } else {
            CloseHandle(thread); // We don't need to wait for the thread
        }
    }
    
    // Cleanup
    closesocket(server_socket);
    WSACleanup();
    CloseHandle(client_mutex);
    
    printf("Server shutdown\n");
    return 0;
}

