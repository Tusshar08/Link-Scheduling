#include <chrono>
#include "socket.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>

int create_listening_socket(const std::string& ip, int port) {
    int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0) {
        perror("socket");
        return -1;
    }
    
    // Set SO_REUSEADDR to allow rebinding
    int opt = 1;
    if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_socket);
        return -1;
    }
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address" << std::endl;
        close(listen_socket);
        return -1;
    }
    
    if (bind(listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_socket);
        return -1;
    }
    
    // Listen
    if (listen(listen_socket, 128) < 0) {
        perror("listen");
        close(listen_socket);
        return -1;
    }
    
    std::cout << "Server listening on " << ip << ":" << port << std::endl;
    return listen_socket;
}

int accept_connection(int listen_socket) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_socket = accept(listen_socket, (struct sockaddr*)&client_addr, &addr_len);
    if (client_socket < 0) {
        perror("accept");
        return -1;
    }
    
    return client_socket;
}

ssize_t send_data(int socket_fd, const void* data, size_t size) {
    return send(socket_fd, data, size, 0);
}

ssize_t recv_data_with_timeout(int socket_fd, void* buffer, size_t size, int timeout_ms) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket_fd, &readfds);
    
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    int select_result = select(socket_fd + 1, &readfds, NULL, NULL, &tv);
    
    if (select_result < 0) {
        perror("select");
        return -1;
    } else if (select_result == 0) {
        // Timeout
        return 0;
    }
    
    return recv(socket_fd, buffer, size, 0);
}

std::string read_header_line(int socket_fd, int timeout_ms) {
    std::string line;
    char buffer[1];

    // A5: use one overall deadline for the complete header read.
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);

    while (line.size() < 1024) {
        const auto now = std::chrono::steady_clock::now();

        if (now >= deadline) {
            return "";
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now
            ).count();

        const int remaining_ms =
            static_cast<int>(remaining > 0 ? remaining : 1);

        ssize_t n =
            recv_data_with_timeout(
                socket_fd,
                buffer,
                1,
                remaining_ms
            );

        if (n < 0) {
            return "";
        }

        if (n == 0) {
            return "";
        }

        line += buffer[0];

        if (buffer[0] == '\n') {
            return line;
        }
    }

    // Header too long.
    return "";
}

bool send_all(int socket_fd, const void* data, size_t size) {
    const char* buffer = (const char*)data;
    size_t bytes_sent = 0;
    
    while (bytes_sent < size) {
        ssize_t n = send(socket_fd, buffer + bytes_sent, size - bytes_sent, 0);
        
        if (n < 0) {
            perror("send");
            return false;
        }
        
        bytes_sent += n;
    }
    
    return true;
}

void close_socket(int socket_fd) {
    if (socket_fd >= 0) {
        close(socket_fd);
    }
}

