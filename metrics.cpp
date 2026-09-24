#include "metrics.h"

#include <utility>


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