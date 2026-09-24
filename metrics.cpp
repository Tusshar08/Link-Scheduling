#include "metrics.h"

#include <fstream>
#include <utility>

bool write_metrics_header(const std::string& path)
{
    std::ofstream out(path, std::ios::out | std::ios::trunc);

    if (!out) {
        return false;
    }

    out << "request_id,op,total_bytes,arrival_ns,start_ns,finish_ns,rounds,forfeited_bytes,latency_ns,service_time_ns\n";

    return out.good();
}

bool append_metric(const std::string& path, const Request& request)
{
    std::ofstream out(path, std::ios::out | std::ios::app);

    if (!out) {
        return false;
    }

    const std::uint64_t latency_ns =
        (request.finish_ns >= request.arrival_ns)
            ? (request.finish_ns - request.arrival_ns)
            : 0;

    const std::uint64_t service_time_ns =
        (request.finish_ns >= request.start_ns)
            ? (request.finish_ns - request.start_ns)
            : 0;

    const char* op_name = "UNKNOWN";
    switch (request.op) {
        case Operation::GET: op_name = "GET"; break;
        case Operation::PUT: op_name = "PUT"; break;
        case Operation::HEALTH: op_name = "HEALTH"; break;
    }

    out << request.request_id << ','
        << op_name << ','
        << request.total_bytes << ','
        << request.arrival_ns << ','
        << request.start_ns << ','
        << request.finish_ns << ','
        << request.rounds << ','
        << request.forfeited_bytes << ','
        << latency_ns << ','
        << service_time_ns << '\n';

    return out.good();
}

void Metrics::record(const Request& request)
{
    RequestMetrics metrics;

    metrics.request_id = request.request_id;
    metrics.op = request.op;

    metrics.total_bytes = request.total_bytes;

    metrics.arrival_ns = request.arrival_ns;
    metrics.start_ns = request.start_ns;
    metrics.finish_ns = request.finish_ns;

    metrics.rounds = request.rounds;
    metrics.forfeited_bytes = request.forfeited_bytes;

    if (request.finish_ns >= request.arrival_ns) {
        metrics.latency_ns =
            request.finish_ns - request.arrival_ns;
    }


    if (request.finish_ns >= request.start_ns) {
        metrics.service_time_ns =
            request.finish_ns - request.start_ns;
    }


    {
        std::lock_guard<std::mutex> lock(mutex_);

        records_.push_back(metrics);

        total_bytes_ += metrics.total_bytes;

        total_latency_ns_ += metrics.latency_ns;

        total_service_time_ns_ +=
            metrics.service_time_ns;

        total_rounds_ += metrics.rounds;

        total_forfeited_bytes_ +=
            metrics.forfeited_bytes;
    }
}


std::size_t Metrics::completed_requests() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return records_.size();
}


std::uint64_t Metrics::total_bytes() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return total_bytes_;
}


double Metrics::average_latency_ns() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (records_.empty()) {
        return 0.0;
    }

    return static_cast<double>(total_latency_ns_) /
           static_cast<double>(records_.size());
}


double Metrics::average_service_time_ns() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (records_.empty()) {
        return 0.0;
    }

    return static_cast<double>(total_service_time_ns_) /
           static_cast<double>(records_.size());
}


double Metrics::average_rounds() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (records_.empty()) {
        return 0.0;
    }

    return static_cast<double>(total_rounds_) /
           static_cast<double>(records_.size());
}


std::uint64_t Metrics::total_forfeited_bytes() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return total_forfeited_bytes_;
}


std::vector<RequestMetrics> Metrics::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return records_;
}


void Metrics::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);

    records_.clear();

    total_bytes_ = 0;
    total_latency_ns_ = 0;
    total_service_time_ns_ = 0;
    total_rounds_ = 0;
    total_forfeited_bytes_ = 0;
}