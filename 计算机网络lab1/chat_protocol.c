#include "chat_protocol.h"

/**
 * Get current timestamp in formatted string
 */
void get_timestamp(char *buffer, size_t size) {
    time_t rawtime;
    struct tm *timeinfo;
    
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", timeinfo);
}

/**
 * Serialize message struct to string format
 * Format: TYPE|TIMESTAMP|USERNAME|CONTENT_LENGTH|CONTENT
 */
int serialize_message(const ChatMessage *msg, char *buffer, size_t buffer_size) {
    if (msg == NULL || buffer == NULL) {
        return -1;
    }
    
    int result = snprintf(buffer, buffer_size, "%d|%s|%s|%d|%.*s",
        msg->type,
        msg->timestamp,
        msg->username,
        msg->content_length,
        msg->content_length,
        msg->content);
    
    if (result < 0 || (size_t)result >= buffer_size) {
        return -1;
    }
    
    return result;
}

/**
 * Deserialize string to message struct
 */
int deserialize_message(const char *buffer, ChatMessage *msg) {
    if (buffer == NULL || msg == NULL) {
        return -1;
    }
    
    memset(msg, 0, sizeof(ChatMessage));
    
    // Parse message format: TYPE|TIMESTAMP|USERNAME|CONTENT_LENGTH|CONTENT
    const char *p = buffer;
    char *endptr;
    
    // Parse type
    msg->type = (MessageType)strtol(p, &endptr, 10);
    if (*endptr != '|') return -1;
    p = endptr + 1;
    
    // Parse timestamp
    const char *timestamp_end = strchr(p, '|');
    if (timestamp_end == NULL) return -1;
    size_t timestamp_len = timestamp_end - p;
    if (timestamp_len >= sizeof(msg->timestamp)) timestamp_len = sizeof(msg->timestamp) - 1;
    strncpy(msg->timestamp, p, timestamp_len);
    msg->timestamp[timestamp_len] = '\0';
    p = timestamp_end + 1;
    
    // Parse username
    const char *username_end = strchr(p, '|');
    if (username_end == NULL) return -1;
    size_t username_len = username_end - p;
    if (username_len >= sizeof(msg->username)) username_len = sizeof(msg->username) - 1;
    strncpy(msg->username, p, username_len);
    msg->username[username_len] = '\0';
    p = username_end + 1;
    
    // Parse content length
    msg->content_length = (int)strtol(p, &endptr, 10);
    if (*endptr != '|') return -1;
    if (msg->content_length < 0 || msg->content_length >= MAX_MESSAGE_LEN) return -1;
    p = endptr + 1;
    
    // Parse content
    if (msg->content_length > 0) {
        if ((size_t)msg->content_length >= sizeof(msg->content)) {
            msg->content_length = sizeof(msg->content) - 1;
        }
        memcpy(msg->content, p, msg->content_length);
        msg->content[msg->content_length] = '\0';
    } else {
        msg->content[0] = '\0';
    }
    
    return 0;
}

/**
 * Print formatted message to console
 */
void print_message(const ChatMessage *msg) {
    if (msg == NULL) return;
    
    const char *type_str;
    switch (msg->type) {
        case MSG_JOIN: type_str = "JOIN"; break;
        case MSG_LEAVE: type_str = "LEAVE"; break;
        case MSG_MESSAGE: type_str = "MESSAGE"; break;
        case MSG_LIST: type_str = "LIST"; break;
        case MSG_ERROR: type_str = "ERROR"; break;
        case MSG_ACK: type_str = "ACK"; break;
        case MSG_SYSTEM: type_str = "SYSTEM"; break;
        default: type_str = "UNKNOWN"; break;
    }
    
    printf("[%s] %s: %s\n", msg->timestamp, msg->username, msg->content);
}


