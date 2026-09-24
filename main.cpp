#include <iostream>
#include <cstring>
#include <thread>
#include <csignal>
#include <vector>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <sys/socket.h>

#include "config.h"
#include "socket.h"
#include "protocol.h"
#include "request.h"
#include "file_io.h"
#include "server.h"
#include "queue.h"
#include "scheduling.h"
#include "metrics.h"

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


// Convert command-line scheduling policy to Scheduler enum.
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

    throw std::runtime_error(
        "invalid scheduling policy: " + policy
    );
}


// Convert Operation enum to readable text.
const char* operation_name(Operation op)
{
    switch (op) {
        case Operation::GET:
            return "GET";

        case Operation::PUT:
            return "PUT";

        case Operation::HEALTH:
            return "HEALTH";
    }

    return "UNKNOWN";
}


// ------------------------------------------------------------
// Acceptor thread
// ------------------------------------------------------------
// The acceptor ONLY:
//   1. accepts connections
//   2. reads/parses request headers
//   3. handles HEALTH immediately
//   4. validates GET/PUT
//   5. puts GET/PUT requests into the queue
//
// It does NOT perform file transfer.
// ------------------------------------------------------------
void acceptor_thread(
    int listen_socket,
    const std::string& file_dir,
    RequestQueue& request_queue)
{
    int request_id = 0;

    while (!shutdown_flag) {

        int client_socket = accept_connection(listen_socket);

        if (client_socket < 0) {
            if (shutdown_flag) {
                break;
            }

            continue;
        }

        // Read request header with a 1-second timeout.
        std::string header =
            read_header_line(client_socket, 1000);

        if (header.empty()) {
            send_error(
                client_socket,
                "timeout or invalid request"
            );

            close_socket(client_socket);
            continue;
        }

        try {
            Request req =
                parse_request_header(
                    header,
                    client_socket
                );

            req.request_id = ++request_id;

            std::cout
                << "Request "
                << req.request_id
                << ": "
                << operation_name(req.op)
                << " "
                << req.filename
                << std::endl;


            // ------------------------------------------------
            // HEALTH
            // ------------------------------------------------
            if (req.op == Operation::HEALTH) {

                send_response(client_socket, request_queue.size());

                std::cout
                    << "  HEALTH check"
                    << std::endl;

                close_socket(client_socket);
                continue;
            }


            // ------------------------------------------------
            // Validate filename
            // ------------------------------------------------
            if (!is_valid_filename(req.filename)) {

                send_error(
                    client_socket,
                    "invalid filename"
                );

                close_socket(client_socket);
                continue;
            }


            // ------------------------------------------------
            // GET
            // ------------------------------------------------
            if (req.op == Operation::GET) {

                std::string file_path =
                    file_dir + "/" + req.filename;

                if (!file_exists(file_path)) {

                    send_error(
                        client_socket,
                        "file not found"
                    );

                    close_socket(client_socket);
                    continue;
                }

                try {
                    req.total_bytes =
                        static_cast<std::uint64_t>(
                            get_file_size(file_path)
                        );
                }
                catch (const std::exception&) {

                    send_error(
                        client_socket,
                        "cannot determine file size"
                    );

                    close_socket(client_socket);
                    continue;
                }
            }


            // ------------------------------------------------
            // PUT
            // ------------------------------------------------
            // For PUT, total_bytes was already parsed from:
            //
            // PUT <filename> <bytes>
            //
            // so no additional size calculation is required.


            // ------------------------------------------------
            // Queue the request
            // ------------------------------------------------

            // A18: arrival is recorded after the request has been
            // fully parsed and is ready to be admitted to the queue.
            req.arrival_ns =
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::nanoseconds
                    >(
                        std::chrono::steady_clock::now()
                            .time_since_epoch()
                    ).count()
                );

            if (!request_queue.push(req)) {

                send_error(
                    client_socket,
                    "server shutting down"
                );

                close_socket(client_socket);
                continue;
            }

            std::cout
                << "  queued request "
                << req.request_id
                << std::endl;

        }
        catch (const std::exception& e) {

            std::cerr
                << "Error parsing request: "
                << e.what()
                << std::endl;

            send_error(
                client_socket,
                e.what()
            );

            close_socket(client_socket);
        }
    }

    // Wake workers and tell them no more requests
    // will be accepted.
    request_queue.close();

    std::cout
        << "Acceptor stopped."
        << std::endl;
}


// ------------------------------------------------------------
// Worker thread
// ------------------------------------------------------------
void worker_thread(
    int worker_id,
    const std::string& file_dir,
    RequestQueue& request_queue,
    Scheduler& scheduler,
    std::mutex& scheduler_mutex,
    const std::string& metrics_out,
    int packetization)
{
    // Scheduler operations are safe here because RequestQueue
    // provides its own synchronization and Scheduler only updates
    // the Request object being processed.
    (void)scheduler_mutex;

    while (true) {

        ScheduleDecision decision = scheduler.next(request_queue);

        if (!decision.valid) {
            break;
        }

        Request req = std::move(decision.request);

        // Record start time only when the worker first picks up
        // this request. RR/DRR rounds must not overwrite it.
        if (req.start_ns == 0) {
            req.start_ns =
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::nanoseconds
                    >(
                        std::chrono::steady_clock::now()
                            .time_since_epoch()
                    ).count()
                );
        }

        std::cout
            << "Worker "
            << worker_id
            << " processing request "
            << req.request_id
            << " ("
            << operation_name(req.op)
            << ", budget="
            << decision.budget
            << ")"
            << std::endl;

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

        }
        else if (req.op == Operation::PUT) {

            // PUT protocol:
            // 1. Server acknowledges before receiving the body.
            // 2. Body is then received according to the scheduler.
            // 3. Final OK 0 is sent after all bytes arrive.
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
            close_socket(req.client_fd);
            continue;
        }

        // Update the scheduler with the actual bytes processed first.
        // This advances the request offset before deciding whether
        // anything remains to be scheduled.
        scheduler.account(req, bytes_processed);

        // Check whether the request still has data remaining.
        bool requeue = scheduler.should_requeue(req);

        // RR forfeits unused allowance when a GET is preempted and
        // requeued. DRR retains unused allowance as deficit.
        // PUT requests always have zero forfeited bytes.
        if (requeue &&
            scheduler.policy() == SchedulingPolicy::RR &&
            req.op == Operation::GET &&
            bytes_processed < decision.budget) {

            req.forfeited_bytes +=
                decision.budget - bytes_processed;
        }

        if (requeue) {

            std::cout
                << "Request "
                << req.request_id
                << " requeued at offset "
                << req.offset
                << "/"
                << req.total_bytes
                << std::endl;

            if (!request_queue.requeue(std::move(req))) {

                std::cerr
                    << "Failed to requeue request "
                    << req.request_id
                    << std::endl;

                close_socket(req.client_fd);
            }

            continue;
        }

        req.finish_ns =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::nanoseconds
                >(
                    std::chrono::steady_clock::now()
                        .time_since_epoch()
                ).count()
            );

        // PUT sends the final OK 0 only after all bytes arrive.
        if (req.op == Operation::PUT) {
            send_response(req.client_fd, 0);
        }

        std::cout
            << "Request "
            << req.request_id
            << " completed"
            << std::endl;

        if (!append_metric(metrics_out, req)) {
            std::cerr
                << "Warning: failed to write metrics for request "
                << req.request_id
                << std::endl;
        }

        close_socket(req.client_fd);
    }

    std::cout
        << "Worker "
        << worker_id
        << " stopped"
        << std::endl;
}

// Main
// ------------------------------------------------------------
int main(int argc, char* argv[])
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);


    // --------------------------------------------------------
    // Default values
    // --------------------------------------------------------
    std::string config_path = "config.json";
    std::string sched_policy;
    std::string file_dir;
    std::string metrics_out = "metrics.csv";

    std::size_t quantum = 0;
    int packetization = 1;


    // --------------------------------------------------------
    // Parse command line
    // --------------------------------------------------------
    for (int i = 1; i < argc; ++i) {

        if (std::strcmp(argv[i], "--config") == 0 &&
            i + 1 < argc) {

            config_path = argv[++i];
        }

        else if (
            std::strcmp(argv[i], "--sched") == 0 &&
            i + 1 < argc) {

            sched_policy = argv[++i];
        }

        else if (
            std::strcmp(argv[i], "--file") == 0 &&
            i + 1 < argc) {

            file_dir = argv[++i];
        }

        else if (
            std::strcmp(argv[i], "--quantum") == 0 &&
            i + 1 < argc) {

            quantum = std::stoull(argv[++i]);
        }

        else if (
            std::strcmp(argv[i], "--p") == 0 &&
            i + 1 < argc) {

            packetization = std::stoi(argv[++i]);
        }

        else if (
            std::strcmp(argv[i], "--metrics-out") == 0 &&
            i + 1 < argc) {

            metrics_out = argv[++i];
        }

        else {
            std::cerr
                << "error: unknown or incomplete argument: "
                << argv[i]
                << std::endl;

            return 1;
        }
    }


    // --------------------------------------------------------
    // Validate required flags
    // --------------------------------------------------------
    if (sched_policy.empty()) {

        std::cerr
            << "error: missing required flag '--sched'"
            << std::endl;

        return 1;
    }

    if (file_dir.empty()) {

        std::cerr
            << "error: missing required flag '--file'"
            << std::endl;

        return 1;
    }


    // --------------------------------------------------------
    // Validate scheduling policy
    // --------------------------------------------------------
    SchedulingPolicy policy;

    try {
        policy = parse_scheduling_policy(
            sched_policy
        );
    }
    catch (const std::exception& e) {

        std::cerr
            << "error: "
            << e.what()
            << std::endl;

        return 1;
    }


    // --------------------------------------------------------
    // Validate quantum
    // --------------------------------------------------------
    if (
        (sched_policy == "rr" ||
         sched_policy == "drr") &&
        quantum == 0
    ) {

        std::cerr
            << "error: --quantum required for "
            << sched_policy
            << std::endl;

        return 1;
    }

    if (
        (sched_policy == "fcfs" ||
         sched_policy == "sjf") &&
        quantum != 0
    ) {

        std::cerr
            << "error: --quantum not allowed for "
            << sched_policy
            << std::endl;

        return 1;
    }


    // --------------------------------------------------------
    // Load configuration
    // --------------------------------------------------------
    Config cfg;

    try {

        cfg = load_config(config_path);

    }
    catch (const std::exception& e) {

        std::cerr
            << e.what()
            << std::endl;

        return 1;
    }


    // --------------------------------------------------------
    // Create scheduler and request queue
    // --------------------------------------------------------
    RequestQueue request_queue;

    Scheduler scheduler(
        policy,
        quantum
    );
    
    std::mutex scheduler_mutex;

    if (!write_metrics_header(metrics_out)) {
        std::cerr
            << "Error: could not create metrics file: "
            << metrics_out
            << std::endl;
        return 1;
    }


    // --------------------------------------------------------
    // Startup information
    // --------------------------------------------------------
    std::cout
        << "Starting server..."
        << std::endl;

    std::cout
        << "Policy: "
        << sched_policy
        << std::endl;

    std::cout
        << "File directory: "
        << file_dir
        << std::endl;

    std::cout
        << "Threads: "
        << cfg.server.server_threads
        << std::endl;

    std::cout
        << "Metrics output: "
        << metrics_out
        << std::endl;

    std::cout
        << "Packetization: "
        << packetization
        << " lines"
        << std::endl;


    // --------------------------------------------------------
    // Create listening socket
    // --------------------------------------------------------
    int listen_socket =
        create_listening_socket(
            cfg.server.ip,
            cfg.server.port
        );

    if (listen_socket < 0) {

        std::cerr
            << "Failed to create listening socket"
            << std::endl;

        return 1;
    }

    global_listen_socket = listen_socket;


    std::cout
        << "Server started! Listening on "
        << cfg.server.ip
        << ":"
        << cfg.server.port
        << std::endl;


    // --------------------------------------------------------
    // Start worker threads
    // --------------------------------------------------------
    std::vector<std::thread> workers;

    workers.reserve(
        cfg.server.server_threads
    );

    for (
        int i = 0;
        i < cfg.server.server_threads;
        ++i
    ) {

        workers.emplace_back(
            worker_thread,
            i + 1,
            std::cref(file_dir),
            std::ref(request_queue),
            std::ref(scheduler),
            std::ref(scheduler_mutex),
            std::cref(metrics_out),
            packetization
        );
    }


    // --------------------------------------------------------
    // Start acceptor
    // --------------------------------------------------------
    std::cout
        << "Waiting for connections "
        << "(Ctrl+C to stop)..."
        << std::endl;

    std::thread acceptor(
        acceptor_thread,
        listen_socket,
        std::cref(file_dir),
        std::ref(request_queue)
    );


    // --------------------------------------------------------
    // Wait for acceptor
    // --------------------------------------------------------
    acceptor.join();


    // --------------------------------------------------------
    // Wait for workers
    // --------------------------------------------------------
    for (std::thread& worker : workers) {

        if (worker.joinable()) {
            worker.join();
        }
    }


    // --------------------------------------------------------
    // Shutdown
    // --------------------------------------------------------
    close_socket(listen_socket);

    global_listen_socket = -1;

    std::cout
        << "Server shutdown complete."
        << std::endl;

    return 0;
}
