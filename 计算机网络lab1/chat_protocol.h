#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_BUFFER_SIZE 4096
#define MAX_USERNAME_LEN 64
#define MAX_MESSAGE_LEN 2048
#define MAX_CLIENTS 100
#define SERVER_PORT 8888

// Message types
typedef enum {
    MSG_JOIN = 1,      // Client joins the chat room
    MSG_LEAVE = 2,     // Client leaves the chat room
    MSG_MESSAGE = 3,   // Chat message
    MSG_LIST = 4,      // Request user list
    MSG_ERROR = 5,     // Error message
    MSG_ACK = 6,       // Acknowledgment
    MSG_SYSTEM = 7,    // System message
    MSG_NICKNAME = 8   // Set nickname (before joining)
} MessageType;

// Message structure
typedef struct {
    MessageType type;
    char timestamp[32];    // Format: YYYY-MM-DD HH:MM:SS
    char username[MAX_USERNAME_LEN];
    char content[MAX_MESSAGE_LEN];
    int content_length;    // Actual content length in bytes
} ChatMessage;

// Function prototypes
void get_timestamp(char *buffer, size_t size);
int serialize_message(const ChatMessage *msg, char *buffer, size_t buffer_size);
int deserialize_message(const char *buffer, ChatMessage *msg);
void print_message(const ChatMessage *msg);

#endif // CHAT_PROTOCOL_H

