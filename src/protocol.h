#pragma once
#include <cstdint>


#include <string>
#include "request.h"

// Parse request header: "GET filename" or "PUT filename 1024"
Request parse_request_header(const std::string& header_line, int socket_fd);

// Send response to client (value parameter)
void send_response(int socket_fd, std::uint64_t value);

// Send error response
void send_error(int socket_fd, const std::string& reason);

