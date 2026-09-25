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
    std::string filename;
    std::uint64_t total_bytes = 0;
    std::uint64_t arrival_ns = 0;
    std::uint64_t start_ns = 0;
    std::uint64_t finish_ns = 0;
    std::uint64_t rounds = 0;
    std::uint64_t forfeited_bytes = 0;
    std::uint64_t a14_events = 0;
};

class Metrics {
public:
    void record(const Request& request);
    std::size_t completed_requests() const;
    std::vector<RequestMetrics> snapshot() const;
    bool write_csv(const std::string& path) const;
    void print_summary() const;

private:
    mutable std::mutex mutex_;
    std::vector<RequestMetrics> records_;
};

#endif
