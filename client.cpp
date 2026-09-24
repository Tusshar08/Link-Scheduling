#include <arpa/inet.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

int connect_to_server(const std::string& ip, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("socket");
        return -1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid server IP\n";
        close(sock);
        return -1;
    }

    if (connect(sock,
                reinterpret_cast<sockaddr*>(&server_addr),
                sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    return sock;
}

bool send_all(int sock, const std::string& data)
{
    size_t total = 0;

    while (total < data.size()) {
        ssize_t sent = send(
            sock,
            data.data() + total,
            data.size() - total,
            0
        );

        if (sent <= 0) {
            return false;
        }

        total += static_cast<size_t>(sent);
    }

    return true;
}

std::string receive_response(int sock)
{
    char buffer[4096];
    std::string response;

    while (true) {
        ssize_t received = recv(sock, buffer, sizeof(buffer), 0);

        if (received <= 0) {
            break;
        }

        response.append(buffer, static_cast<size_t>(received));

        if (response.find('\n') != std::string::npos) {
            break;
        }
    }

    return response;
}

int health()
{
    int sock = connect_to_server("127.0.0.1", 9000);

    if (sock < 0) {
        return 1;
    }

    if (!send_all(sock, "HEALTH\n")) {
        std::cerr << "Failed to send HEALTH request\n";
        close(sock);
        return 1;
    }

    std::string response = receive_response(sock);

    std::cout << response;

    close(sock);
    return 0;
}

int get_file(const std::string& filename)
{
    int sock = connect_to_server("127.0.0.1", 9000);

    if (sock < 0) {
        return 1;
    }

    std::string request = "GET " + filename + "\n";

    if (!send_all(sock, request)) {
        std::cerr << "Failed to send GET request\n";
        close(sock);
        return 1;
    }

    // Receive the response header: OK <size>\n or ERR <reason>\n
    std::string response = receive_response(sock);
    std::cout << response;

    if (response.rfind("OK ", 0) != 0) {
        close(sock);
        return 1;
    }

    // Extract file size from "OK <size>\n"
    std::uint64_t file_size = 0;

    try {
        std::string size_text = response.substr(3);
        file_size = std::stoull(size_text);
    }
    catch (...) {
        std::cerr << "Invalid GET response\n";
        close(sock);
        return 1;
    }

    // Receive the actual file contents.
    std::uint64_t received = 0;
    char buffer[8192];

    while (received < file_size) {

        std::uint64_t remaining = file_size - received;
        std::size_t to_receive =
            static_cast<std::size_t>(
                std::min<std::uint64_t>(
                    remaining,
                    sizeof(buffer)
                )
            );

        ssize_t n = recv(
            sock,
            buffer,
            to_receive,
            0
        );

        if (n <= 0) {
            std::cerr << "\nConnection closed before "
                      << "the complete file was received\n";
            close(sock);
            return 1;
        }

        std::cout.write(buffer, n);
        received += static_cast<std::uint64_t>(n);
    }

    std::cout << "\nReceived "
              << received
              << " bytes\n";

    close(sock);
    return 0;
}

int put_file(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary);

    if (!file) {
        std::cerr << "Cannot open file: " << filename << "\n";
        return 1;
    }

    file.seekg(0, std::ios::end);
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string data(static_cast<size_t>(file_size), '\0');

    if (file_size > 0) {
        file.read(data.data(), file_size);
    }

    file.close();

    int sock = connect_to_server("127.0.0.1", 9000);

    if (sock < 0) {
        return 1;
    }

    std::string header =
        "PUT " + filename + " " +
        std::to_string(data.size()) + "\n";

    /*
     * PUT protocol:
     * 1. Send the header.
     * 2. Wait for the server's initial OK 0.
     * 3. Send the file body.
     * 4. Wait for the final OK 0.
     */

    if (!send_all(sock, header)) {
        std::cerr << "Failed to send PUT header\n";
        close(sock);
        return 1;
    }

    std::string initial_response = receive_response(sock);

    if (initial_response.rfind("OK ", 0) != 0) {
        std::cerr << "PUT rejected: "
                  << initial_response;
        close(sock);
        return 1;
    }

    std::cout << initial_response;

    if (!data.empty() && !send_all(sock, data)) {
        std::cerr << "Failed to send file data\n";
        close(sock);
        return 1;
    }

    std::string final_response = receive_response(sock);

    std::cout << final_response;

    if (final_response != "OK 0\n") {
        std::cerr << "PUT did not complete successfully\n";
        close(sock);
        return 1;
    }

    close(sock);
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage:\n";
        std::cerr << "  ./client health\n";
        std::cerr << "  ./client get <filename>\n";
        std::cerr << "  ./client put <filename>\n";
        return 1;
    }

    std::string command = argv[1];

    if (command == "health") {
        return health();
    }

    if (command == "get") {
        if (argc != 3) {
            std::cerr << "Usage: ./client get <filename>\n";
            return 1;
        }

        return get_file(argv[2]);
    }

    if (command == "put") {
        if (argc != 3) {
            std::cerr << "Usage: ./client put <filename>\n";
            return 1;
        }

        return put_file(argv[2]);
    }

    std::cerr << "Unknown command: " << command << "\n";
    return 1;
}
