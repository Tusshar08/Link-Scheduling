#include "metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

void Metrics::record(const Request& request)
{
    RequestMetrics metrics;
    metrics.request_id = request.request_id;
    metrics.op = request.op;
    metrics.filename = request.filename;
    metrics.total_bytes = request.total_bytes;
    metrics.arrival_ns = request.arrival_ns;
    metrics.start_ns = request.start_ns;
    metrics.finish_ns = request.finish_ns;
    metrics.rounds = request.rounds;
    metrics.forfeited_bytes = request.forfeited_bytes;
    metrics.a14_events = request.a14_events;

    std::lock_guard<std::mutex> lock(mutex_);
    records_.push_back(std::move(metrics));
}

std::size_t Metrics::completed_requests() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return records_.size();
}

std::vector<RequestMetrics> Metrics::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

bool Metrics::write_csv(const std::string& path) const
{
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        return false;
    }

    out << "request_id,op,filename,bytes,rounds,"
        << "forfeited_bytes,arrival_ns,start_ns,finish_ns\n";

    auto rows = snapshot();
    std::sort(
        rows.begin(),
        rows.end(),
        [](const RequestMetrics& a, const RequestMetrics& b) {
            return a.request_id < b.request_id;
        }
    );

    for (const RequestMetrics& row : rows) {
        const char* op_name = "UNKNOWN";
        if (row.op == Operation::GET) {
            op_name = "GET";
        } else if (row.op == Operation::PUT) {
            op_name = "PUT";
        }

        out << row.request_id << ','
            << op_name << ','
            << row.filename << ','
            << row.total_bytes << ','
            << row.rounds << ','
            << row.forfeited_bytes << ','
            << row.arrival_ns << ','
            << row.start_ns << ','
            << row.finish_ns
            << '\n';
    }

    return out.good();
}

namespace {

std::uint64_t nearest_rank(
    std::vector<std::uint64_t> values,
    double percentile)
{
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    const std::size_t index =
        static_cast<std::size_t>(
            std::ceil(percentile / 100.0 * static_cast<double>(n))
        ) - 1;
    return values[std::min(index, n - 1)];
}

}  // namespace

void Metrics::print_summary() const
{
    auto rows = snapshot();

    std::cout << "=== aggregate summary ===" << std::endl;
    std::cout << "completed_requests: " << rows.size() << std::endl;

    if (rows.empty()) {
        return;
    }

    std::vector<std::uint64_t> waiting;
    std::vector<std::uint64_t> response;
    waiting.reserve(rows.size());
    response.reserve(rows.size());

    std::uint64_t min_arrival = rows.front().arrival_ns;
    std::uint64_t max_finish = rows.front().finish_ns;
    std::uint64_t a14 = 0;
    std::uint64_t forfeited = 0;

    for (const RequestMetrics& row : rows) {
        min_arrival = std::min(min_arrival, row.arrival_ns);
        max_finish = std::max(max_finish, row.finish_ns);
        a14 += row.a14_events;
        forfeited += row.forfeited_bytes;

        const std::uint64_t wait =
            (row.start_ns >= row.arrival_ns)
                ? (row.start_ns - row.arrival_ns)
                : 0;
        const std::uint64_t resp =
            (row.finish_ns >= row.arrival_ns)
                ? (row.finish_ns - row.arrival_ns)
                : 0;
        waiting.push_back(wait);
        response.push_back(resp);
    }

    const double window_s =
        static_cast<double>(max_finish - min_arrival) / 1e9;
    const double throughput =
        window_s > 0.0
            ? static_cast<double>(rows.size()) / window_s
            : 0.0;

    std::cout << "waiting_p50_ns: "
              << nearest_rank(waiting, 50.0) << std::endl;
    std::cout << "waiting_p99_ns: "
              << nearest_rank(waiting, 99.0) << std::endl;
    std::cout << "response_p50_ns: "
              << nearest_rank(response, 50.0) << std::endl;
    std::cout << "response_p99_ns: "
              << nearest_rank(response, 99.0) << std::endl;
    std::cout << "throughput_rps: " << throughput << std::endl;
    std::cout << "a14_events: " << a14 << std::endl;
    std::cout << "forfeited_bytes: " << forfeited << std::endl;
}
