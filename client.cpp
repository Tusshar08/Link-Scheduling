#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "config.h"
#include "socket.h"

namespace fs = std::filesystem;

bool send_all_str(int sock, const std::string& data)
{
    return send_all(sock, data.data(), data.size());
}

int connect_to_server(const Config& cfg)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(cfg.server.port));

    if (inet_pton(AF_INET, cfg.server.ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid server IP\n";
        close(sock);
        return -1;
    }

    if (connect(
            sock,
            reinterpret_cast<sockaddr*>(&server_addr),
            sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    return sock;
}

bool read_status_line(
    int sock,
    std::string& line,
    std::vector<char>& leftover)
{
    leftover.clear();
    line.clear();
    char buffer[4096];

    while (line.size() < 8192) {
        const ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return false;
        }
        for (ssize_t i = 0; i < n; ++i) {
            line.push_back(buffer[i]);
            if (buffer[i] == '\n') {
                leftover.assign(buffer + i + 1, buffer + n);
                return true;
            }
        }
    }
    return false;
}

bool recv_exact(
    int sock,
    std::uint64_t size,
    std::vector<char>& prefix,
    std::ostream* out)
{
    std::uint64_t received = 0;
    std::size_t prefix_off = 0;

    while (received < size && prefix_off < prefix.size()) {
        const std::size_t chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                size - received,
                prefix.size() - prefix_off
            )
        );
        if (out) {
            out->write(prefix.data() + prefix_off, static_cast<std::streamsize>(chunk));
            if (!*out) {
                return false;
            }
        }
        prefix_off += chunk;
        received += chunk;
    }

    char buffer[8192];
    while (received < size) {
        const std::size_t chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(size - received, sizeof(buffer))
        );
        const ssize_t n = recv(sock, buffer, chunk, 0);
        if (n <= 0) {
            return false;
        }
        if (out) {
            out->write(buffer, n);
            if (!*out) {
                return false;
            }
        }
        received += static_cast<std::uint64_t>(n);
    }
    return true;
}

int get_file(
    const Config& cfg,
    const std::string& filename,
    bool quiet,
    bool write_to_disk)
{
    const int sock = connect_to_server(cfg);
    if (sock < 0) {
        return 1;
    }

    if (!send_all_str(sock, "GET " + filename + "\n")) {
        close(sock);
        return 1;
    }

    std::string response;
    std::vector<char> leftover;
    if (!read_status_line(sock, response, leftover)) {
        close(sock);
        return 1;
    }

    if (response.rfind("OK ", 0) != 0) {
        if (!quiet) {
            std::cerr << response;
        }
        close(sock);
        return 1;
    }

    std::uint64_t file_size = 0;
    try {
        file_size = std::stoull(response.substr(3));
    } catch (...) {
        close(sock);
        return 1;
    }

    std::ofstream output;
    std::ostream* sink = nullptr;
    if (write_to_disk) {
        output.open(filename, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "Cannot create output file: " << filename << "\n";
            close(sock);
            return 1;
        }
        sink = &output;
    }

    const bool ok = recv_exact(sock, file_size, leftover, sink);
    if (write_to_disk) {
        output.close();
    }
    close(sock);

    if (!ok) {
        std::cerr << "Connection closed before complete file was received\n";
        return 1;
    }

    if (!quiet) {
        std::cout << "GET " << filename << ": "
                  << file_size << " bytes received\n";
    }
    return 0;
}

int put_file(const Config& cfg, const std::string& local_path, bool quiet)
{
    std::ifstream file(local_path, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open file: " << local_path << "\n";
        return 1;
    }

    file.seekg(0, std::ios::end);
    const std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (file_size < 0) {
        std::cerr << "Cannot determine file size\n";
        return 1;
    }

    std::string data(static_cast<std::size_t>(file_size), '\0');
    if (file_size > 0) {
        file.read(data.data(), file_size);
    }
    file.close();

    const std::string wire_name = fs::path(local_path).filename().string();
    if (wire_name.empty() || wire_name == "." || wire_name == "..") {
        std::cerr << "Invalid filename\n";
        return 1;
    }

    const int sock = connect_to_server(cfg);
    if (sock < 0) {
        return 1;
    }

    const std::string header =
        "PUT " + wire_name + " " + std::to_string(data.size()) + "\n";
    if (!send_all_str(sock, header)) {
        close(sock);
        return 1;
    }

    std::string initial;
    std::vector<char> leftover;
    if (!read_status_line(sock, initial, leftover) ||
        initial.rfind("OK ", 0) != 0) {
        if (!quiet) {
            std::cerr << initial;
        }
        close(sock);
        return 1;
    }

    if (!data.empty() && !send_all_str(sock, data)) {
        std::cerr << "Failed to send file data\n";
        close(sock);
        return 1;
    }

    std::string final_response;
    std::vector<char> leftover2;
    if (!read_status_line(sock, final_response, leftover2)) {
        close(sock);
        return 1;
    }
    close(sock);

    if (final_response != "OK 0\n") {
        if (!quiet) {
            std::cerr << final_response;
        }
        return 1;
    }

    if (!quiet) {
        std::cout << "PUT " << wire_name << ": "
                  << data.size() << " bytes sent\n";
    }
    return 0;
}

int health(const Config& cfg)
{
    const int sock = connect_to_server(cfg);
    if (sock < 0) {
        return 1;
    }
    if (!send_all_str(sock, "HEALTH\n")) {
        close(sock);
        return 1;
    }
    std::string response;
    std::vector<char> leftover;
    if (!read_status_line(sock, response, leftover)) {
        close(sock);
        return 1;
    }
    std::cout << response;
    close(sock);
    return 0;
}

struct WorkloadFile {
    std::string path;
    std::string name;
};

std::vector<WorkloadFile> list_workload(const std::string& dir)
{
    std::vector<WorkloadFile> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        WorkloadFile item;
        item.path = entry.path().string();
        item.name = entry.path().filename().string();
        if (item.name.empty() || item.name[0] == '.') {
            continue;
        }
        files.push_back(std::move(item));
    }
    std::sort(files.begin(), files.end(),
              [](const WorkloadFile& a, const WorkloadFile& b) {
                  return a.name < b.name;
              });
    return files;
}

int load_workload(
    const Config& cfg,
    const std::string& dir,
    int requests)
{
    const std::vector<WorkloadFile> files = list_workload(dir);
    if (files.empty()) {
        std::cerr << "error: no files in workload directory\n";
        return 1;
    }

    for (const WorkloadFile& file : files) {
        if (put_file(cfg, file.path, true) != 0) {
            std::cerr << "error: seeding failed for " << file.path << "\n";
            return 1;
        }
    }

    std::atomic<int> next{0};
    std::atomic<int> failures{0};
    const int thread_count = cfg.server.client_threads;
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(thread_count));

    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng{
                std::random_device{}() ^
                static_cast<unsigned>(t * 2654435761u)
            };
            std::uniform_int_distribution<std::size_t> file_dist(
                0,
                files.size() - 1
            );
            std::uniform_int_distribution<int> op_dist(0, 1);

            while (true) {
                const int i = next.fetch_add(1);
                if (i >= requests) {
                    break;
                }
                const WorkloadFile& file = files[file_dist(rng)];
                if (op_dist(rng) == 0) {
                    if (get_file(cfg, file.name, true, false) != 0) {
                        failures.fetch_add(1);
                    }
                } else {
                    if (put_file(cfg, file.path, true) != 0) {
                        failures.fetch_add(1);
                    }
                }
            }
        });
    }

    for (std::thread& thread : threads) {
        thread.join();
    }
    if (failures != 0) {
        std::cerr << "error: " << failures
                  << " load requests failed\n";
        return 1;
    }
    return 0;
}

void usage()
{
    std::cerr
        << "Usage:\n"
        << "  ./client [--config path] put <local-path>\n"
        << "  ./client [--config path] get <name>\n"
        << "  ./client [--config path] load <workload-dir> --requests N\n";
}

int main(int argc, char* argv[])
{
    std::string config_path = "config.json";
    std::string command;
    std::vector<std::string> positional;
    int requests = -1;
    bool requests_set = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "error: missing value for --config\n";
                return 1;
            }
            config_path = argv[++i];
        } else if (std::strcmp(argv[i], "--requests") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "error: missing value for --requests\n";
                return 1;
            }
            try {
                requests = std::stoi(argv[++i]);
            } catch (...) {
                std::cerr << "error: invalid --requests\n";
                return 1;
            }
            requests_set = true;
        } else if (argv[i][0] == '-') {
            std::cerr << "error: unknown flag " << argv[i] << "\n";
            return 1;
        } else {
            positional.push_back(argv[i]);
        }
    }

    if (positional.empty()) {
        usage();
        return 1;
    }
    command = positional[0];

    if (requests_set && command != "load") {
        std::cerr << "error: --requests is only valid with load\n";
        return 1;
    }

    Config cfg;
    try {
        cfg = load_config(config_path);
    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << "\n";
        return 1;
    }

    if (command == "health") {
        return health(cfg);
    }

    if (command == "get") {
        if (positional.size() != 2) {
            usage();
            return 1;
        }
        return get_file(cfg, positional[1], false, true);
    }

    if (command == "put") {
        if (positional.size() != 2) {
            usage();
            return 1;
        }
        return put_file(cfg, positional[1], false);
    }

    if (command == "load") {
        if (positional.size() != 2) {
            usage();
            return 1;
        }
        if (!requests_set || requests <= 0) {
            std::cerr << "error: load requires --requests N\n";
            return 1;
        }
        return load_workload(cfg, positional[1], requests);
    }

    std::cerr << "Unknown command: " << command << "\n";
    usage();
    return 1;
}
