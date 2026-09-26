#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/select.h>
#include <thread>
#include <vector>
#include <cstdio>

#include "clock_ns.h"
#include "config.h"
#include "file_io.h"
#include "metrics.h"
#include "protocol.h"
#include "queue.h"
#include "scheduling.h"
#include "server.h"
#include "socket.h"

volatile std::sig_atomic_t shutdown_flag = 0;
int global_listen_socket = -1;

void handle_signal(int signal)
{
    if (signal == SIGINT || signal == SIGTERM) {
        shutdown_flag = 1;
        if (global_listen_socket >= 0) {
            shutdown(global_listen_socket, SHUT_RDWR);
        }
    }
}

SchedulingPolicy parse_scheduling_policy(const std::string& policy)
{
    if (policy == "fcfs") {
        return SchedulingPolicy::FCFS;
    }
    if (policy == "sjf") {
        return SchedulingPolicy::SJF;
    }
    if (policy == "rr") {
        return SchedulingPolicy::RR;
    }
    if (policy == "drr") {
        return SchedulingPolicy::DRR;
    }
    throw std::runtime_error("invalid scheduling policy: " + policy);
}

void parser_thread(
    FdQueue& fd_queue,
    RequestQueue& request_queue,
    const std::string& file_dir,
    std::atomic<int>& next_request_id)
{
    int client_socket = -1;

    while (fd_queue.wait_pop(client_socket)) {
        std::vector<char> leftover;
        const std::string header =
            read_header_line(client_socket, 1000, leftover);

        if (header.empty()) {
            send_error(client_socket, "timeout or invalid request");
            close_socket(client_socket);
            continue;
        }

        try {
            Request req = parse_request_header(header, client_socket);
            req.pending_bytes = std::move(leftover);

            if (req.op == Operation::HEALTH) {
                send_response(client_socket, request_queue.size());
                close_socket(client_socket);
                continue;
            }

            if (!is_valid_filename(req.filename)) {
                send_error(client_socket, "invalid filename");
                close_socket(client_socket);
                continue;
            }

            if (req.op == Operation::GET) {
                const std::string file_path = file_dir + "/" + req.filename;
                if (!file_exists(file_path)) {
                    send_error(client_socket, "file not found");
                    close_socket(client_socket);
                    continue;
                }
                try {
                    req.total_bytes = static_cast<std::uint64_t>(
                        get_file_size(file_path)
                    );
                } catch (const std::exception&) {
                    send_error(client_socket, "cannot determine file size");
                    close_socket(client_socket);
                    continue;
                }
            }

            req.request_id = next_request_id.fetch_add(1);
            req.arrival_ns = monotonic_ns();

            if (!request_queue.push(req)) {
                send_error(client_socket, "server shutting down");
                close_socket(client_socket);
            }
        } catch (const std::exception& e) {
            send_error(client_socket, e.what());
            close_socket(client_socket);
        }
    }
}

void acceptor_thread(int listen_socket, FdQueue& fd_queue)
{
    while (!shutdown_flag) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_socket, &readfds);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        const int ready = select(
            listen_socket + 1,
            &readfds,
            nullptr,
            nullptr,
            &timeout
        );
        if (ready == 0) {
            continue;
        }
        if (ready < 0) {
            if (shutdown_flag) {
                break;
            }
            continue;
        }

        const int client_socket = accept_connection(listen_socket);
        if (client_socket < 0) {
            if (shutdown_flag) {
                break;
            }
            continue;
        }
        if (!fd_queue.push(client_socket)) {
            close_socket(client_socket);
        }
    }

    fd_queue.close();
}

void worker_thread(
    const std::string& file_dir,
    RequestQueue& request_queue,
    Scheduler& scheduler,
    Metrics& metrics,
    int packetization)
{
    while (true) {
        ScheduleDecision decision = scheduler.next(request_queue);
        if (!decision.valid) {
            break;
        }

        Request req = std::move(decision.request);
        if (req.start_ns == 0) {
            req.start_ns = monotonic_ns();
        }

        std::uint64_t bytes_processed = 0;
        bool success = false;

        if (req.op == Operation::GET) {
            success = serve_get(
                req,
                file_dir,
                decision.budget,
                bytes_processed,
                scheduler.policy() == SchedulingPolicy::RR,
                packetization
            );
        } else if (req.op == Operation::PUT) {
            if (req.offset == 0) {
                send_response(req.client_fd, 0);
            }
            success = serve_put(
                req,
                file_dir,
                decision.budget,
                bytes_processed
            );
        }

        if (!success) {
            if (!req.staging_filename.empty()) {
                std::remove((file_dir + "/" + req.staging_filename).c_str());
            }
            request_queue.complete();
            close_socket(req.client_fd);
            continue;
        }

        scheduler.account(req, bytes_processed);
        const bool requeue = scheduler.should_requeue(req);

        if (requeue &&
            scheduler.policy() == SchedulingPolicy::RR &&
            req.op == Operation::GET &&
            bytes_processed < decision.budget) {
            req.forfeited_bytes += decision.budget - bytes_processed;
        }

        if (requeue) {
            if (!request_queue.requeue(std::move(req))) {
                close_socket(req.client_fd);
            }
            continue;
        }

        if (req.op == Operation::PUT) {
            const std::string staging_path =
                file_dir + "/" + req.staging_filename;
            const std::string final_path =
                file_dir + "/" + req.filename;
            if (std::rename(staging_path.c_str(), final_path.c_str()) != 0) {
                send_error(req.client_fd, "cannot commit file");
                request_queue.complete();
                close_socket(req.client_fd);
                continue;
            }
            send_response(req.client_fd, 0);
        }
        req.finish_ns = monotonic_ns();
        metrics.record(req);
        request_queue.complete();
        close_socket(req.client_fd);
    }
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    std::string config_path = "config.json";
    std::string sched_policy;
    std::string file_dir;
    std::string metrics_out = "metrics.csv";
    std::size_t quantum = 0;
    int packetization = 1;
    bool quantum_set = false;

    for (int i = 1; i < argc; ++i) {
        auto need_value = [&](const char* flag) {
            if (i + 1 >= argc) {
                std::cerr << "error: missing value for " << flag << std::endl;
                std::exit(1);
            }
        };

        if (std::strcmp(argv[i], "--config") == 0) {
            need_value("--config");
            config_path = argv[++i];
        } else if (std::strcmp(argv[i], "--sched") == 0) {
            need_value("--sched");
            sched_policy = argv[++i];
        } else if (std::strcmp(argv[i], "--file") == 0) {
            need_value("--file");
            file_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--quantum") == 0) {
            need_value("--quantum");
            quantum_set = true;
            try {
                quantum = std::stoull(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "error: invalid --quantum" << std::endl;
                return 1;
            }
        } else if (std::strcmp(argv[i], "--p") == 0) {
            need_value("--p");
            try {
                packetization = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "error: invalid --p" << std::endl;
                return 1;
            }
        } else if (std::strcmp(argv[i], "--metrics-out") == 0) {
            need_value("--metrics-out");
            metrics_out = argv[++i];
        } else {
            std::cerr << "error: unknown or incomplete argument: "
                      << argv[i] << std::endl;
            return 1;
        }
    }

    if (sched_policy.empty()) {
        std::cerr << "error: missing required flag '--sched'" << std::endl;
        return 1;
    }
    if (file_dir.empty()) {
        std::cerr << "error: missing required flag '--file'" << std::endl;
        return 1;
    }
    if (packetization <= 0) {
        std::cerr << "error: --p must be a positive integer" << std::endl;
        return 1;
    }

    SchedulingPolicy policy;
    try {
        policy = parse_scheduling_policy(sched_policy);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }

    if ((sched_policy == "rr" || sched_policy == "drr") &&
        (!quantum_set || quantum == 0)) {
        std::cerr << "error: --quantum required for "
                  << sched_policy << std::endl;
        return 1;
    }
    if ((sched_policy == "fcfs" || sched_policy == "sjf") && quantum_set) {
        std::cerr << "error: --quantum not allowed for "
                  << sched_policy << std::endl;
        return 1;
    }

    Config cfg;
    try {
        cfg = load_config(config_path);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    RequestQueue request_queue;
    FdQueue fd_queue;
    Scheduler scheduler(policy, quantum);
    Metrics metrics;
    std::atomic<int> next_request_id{1};

    std::cout << "Starting server..." << std::endl;
    std::cout << "Policy: " << sched_policy << std::endl;
    std::cout << "File directory: " << file_dir << std::endl;
    std::cout << "Threads: " << cfg.server.server_threads << std::endl;
    if (sched_policy == "rr" || sched_policy == "drr") {
        std::cout << "Quantum: " << quantum << " bytes" << std::endl;
    }
    std::cout << "Metrics output: " << metrics_out << std::endl;

    const int listen_socket =
        create_listening_socket(cfg.server.ip, cfg.server.port);
    if (listen_socket < 0) {
        std::cerr << "Failed to create listening socket" << std::endl;
        return 1;
    }
    global_listen_socket = listen_socket;

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(cfg.server.server_threads));
    for (int i = 0; i < cfg.server.server_threads; ++i) {
        workers.emplace_back(
            worker_thread,
            std::cref(file_dir),
            std::ref(request_queue),
            std::ref(scheduler),
            std::ref(metrics),
            packetization
        );
    }

    const int parser_count =
        std::max(8, cfg.server.client_threads);
    std::vector<std::thread> parsers;
    parsers.reserve(static_cast<std::size_t>(parser_count));
    for (int i = 0; i < parser_count; ++i) {
        parsers.emplace_back(
            parser_thread,
            std::ref(fd_queue),
            std::ref(request_queue),
            std::cref(file_dir),
            std::ref(next_request_id)
        );
    }

    std::thread acceptor(acceptor_thread, listen_socket, std::ref(fd_queue));
    acceptor.join();

    for (std::thread& parser : parsers) {
        if (parser.joinable()) {
            parser.join();
        }
    }

    request_queue.close();

    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    metrics.print_summary();
    if (!metrics.write_csv(metrics_out)) {
        std::cerr << "error: failed to write " << metrics_out << std::endl;
    }

    close_socket(listen_socket);
    global_listen_socket = -1;
    std::cout << "Server shutdown complete." << std::endl;
    return 0;
}
