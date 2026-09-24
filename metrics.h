#ifndef METRICS_H
#define METRICS_H

#include "queue.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct RequestMetrics {
    int request_id = 0;

    Operation op = Operation::GET;

    std::uint64_t total_bytes = 0;

    std::uint64_t arrival_ns = 0;
    std::uint64_t start_ns = 0;
    std::uint64_t finish_ns = 0;

    std::uint64_t rounds = 0;
    std::uint64_t forfeited_bytes = 0;

    std::uint64_t latency_ns = 0;
    std::uint64_t service_time_ns = 0;
};


class Metrics {
public:
    Metrics() = default;
    ~Metrics() = default;

    void record(const Request& request);

    std::size_t completed_requests() const;

    std::uint64_t total_bytes() const;

    double average_latency_ns() const;

    double average_service_time_ns() const;

    double average_rounds() const;

    std::uint64_t total_forfeited_bytes() const;

    std::vector<RequestMetrics> snapshot() const;

    void reset();

private:
    mutable std::mutex mutex_;

    std::vector<RequestMetrics> records_;

    std::uint64_t total_bytes_ = 0;
    std::uint64_t total_latency_ns_ = 0;
    std::uint64_t total_service_time_ns_ = 0;
    std::uint64_t total_rounds_ = 0;
    std::uint64_t total_forfeited_bytes_ = 0;
};

#endif