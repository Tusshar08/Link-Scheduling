#include "protocol.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "socket.h"

Request parse_request_header(const std::string& header_line, int client_fd)
{
    std::string line = header_line;

    while (!line.empty() &&
           (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }

    if (line.empty()) {
        throw std::runtime_error("empty request");
    }

    std::istringstream iss(line);

    std::string op;
    if (!(iss >> op)) {
        throw std::runtime_error("missing operation");
    }

    for (char& c : op) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c))
        );
    }

    Request request;
    request.client_fd = client_fd;

    if (op == "HEALTH") {
        std::string extra;

        if (iss >> extra) {
            throw std::runtime_error("HEALTH takes no arguments");
        }

        request.op = Operation::HEALTH;
        request.filename.clear();
        request.total_bytes = 0;

        return request;
    }

    if (op == "GET") {
        std::string filename;
        std::string extra;

        if (!(iss >> filename)) {
            throw std::runtime_error("GET requires filename");
        }

        if (iss >> extra) {
            throw std::runtime_error(
                "GET takes exactly one filename"
            );
        }

        request.op = Operation::GET;
        request.filename = filename;
        request.total_bytes = 0;

        return request;
    }

    if (op == "PUT") {
        std::string filename;
        std::string byte_string;
        std::string extra;

        if (!(iss >> filename)) {
            throw std::runtime_error("PUT requires filename");
        }

        if (!(iss >> byte_string)) {
            throw std::runtime_error(
                "PUT requires byte count"
            );
        }

        if (iss >> extra) {
            throw std::runtime_error(
                "PUT takes filename and byte count only"
            );
        }

        if (byte_string.empty() ||
            !std::all_of(
                byte_string.begin(),
                byte_string.end(),
                [](unsigned char c) {
                    return std::isdigit(c);
                })) {
            throw std::runtime_error(
                "invalid byte count"
            );
        }

        std::uint64_t bytes = 0;

        try {
            bytes = std::stoull(byte_string);
        }
        catch (const std::exception&) {
            throw std::runtime_error(
                "invalid byte count"
            );
        }

        request.op = Operation::PUT;
        request.filename = filename;
        request.total_bytes = bytes;

        return request;
    }

    throw std::runtime_error(
        "unknown operation: " + op
    );
}

void send_response(int client_fd, std::uint64_t value)
{
    std::string response =
        "OK " + std::to_string(value) + "\n";

    if (!send_all(
            client_fd,
            response.data(),
            response.size())) {
        std::cerr << "Failed to send response" << std::endl;
    }
}

void send_error(int client_fd, const std::string& reason)
{
    std::string response =
        "ERR " + reason + "\n";

    if (!send_all(
            client_fd,
            response.data(),
            response.size())) {
        std::cerr << "Failed to send error response"
                  << std::endl;
    }
}
