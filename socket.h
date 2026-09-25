#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

// Create listening socket
int create_listening_socket(const std::string& ip, int port);

// Accept connection
int accept_connection(int listen_socket);

// Send data on socket
ssize_t send_data(int socket_fd, const void* data, size_t size);

// Receive data with timeout (milliseconds)
// Returns: bytes read, 0 on timeout, -1 on error
ssize_t recv_data_with_timeout(int socket_fd, void* buffer, size_t size, int timeout_ms);

// Read exactly one line (up to \n) with timeout.
// Extra bytes after the header are returned in leftover.
// Empty string means timeout or error.
std::string read_header_line(
    int socket_fd,
    int timeout_ms,
    std::vector<char>& leftover);

// Send all data (handles partial writes)
// Returns: true if all data sent, false on error
bool send_all(int socket_fd, const void* data, size_t size);

// Close socket
void close_socket(int socket_fd);

