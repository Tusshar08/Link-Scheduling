#include "scheduling.h"

#include <algorithm>
#include <stdexcept>


Scheduler::Scheduler(SchedulingPolicy policy, std::uint64_t quantum) : policy_(policy), quantum_(quantum)
{
    if ((policy_ == SchedulingPolicy::RR ||
         policy_ == SchedulingPolicy::DRR) &&
        quantum_ == 0) {
        throw std::invalid_argument(
            "RR/DRR requires a non-zero quantum"
        );
    }
}


ScheduleDecision Scheduler::next(RequestQueue& queue)
{
    ScheduleDecision decision;

    Request request;

    switch (policy_) {

        case SchedulingPolicy::FCFS:

            if (!queue.wait_pop(request)) {
                return decision;
            }

            break;

        case SchedulingPolicy::SJF:

            if (!queue.wait_pop_best(
                    request,
                    [](const Request& a, const Request& b) {

                        
                        if (a.total_bytes != b.total_bytes) {
                            return a.total_bytes < b.total_bytes;
                        }

                        return a.arrival_ns < b.arrival_ns;
                    })) {

                return decision;
            }

            break;

        case SchedulingPolicy::RR:

            if (!queue.wait_pop(request)) {
                return decision;
            }

            break;

        case SchedulingPolicy::DRR:

            if (!queue.wait_pop(request)) {
                return decision;
            }

            break;
    }


    decision.request = std::move(request);

    decision.budget = get_budget(decision.request);

    decision.valid = true;

    return decision;
}


std::uint64_t Scheduler::get_budget(Request& request)
{
    const std::uint64_t remaining =
        (request.total_bytes > request.offset)
            ? (request.total_bytes - request.offset)
            : 0;


    if (remaining == 0) {
        return 0;
    }


    switch (policy_) {

        case SchedulingPolicy::FCFS:
            return remaining;

        case SchedulingPolicy::SJF:
            return remaining;

        case SchedulingPolicy::RR:

            return std::min(
                remaining,
                quantum_
            );

        case SchedulingPolicy::DRR:
            request.deficit += quantum_;

            return std::min(
                remaining,
                request.deficit
            );
    }


    return 0;
}


void Scheduler::account(
    Request& request,
    std::uint64_t bytes_processed)
{

    request.rounds++;

    const std::uint64_t remaining =
        (request.total_bytes > request.offset)
            ? (request.total_bytes - request.offset)
            : 0;

    const std::uint64_t actual =
        std::min(bytes_processed, remaining);

    request.offset += actual;

    if (policy_ == SchedulingPolicy::DRR) {

        if (actual >= request.deficit) {
            request.deficit = 0;
        }
        else {
            request.deficit -= actual;
        }
    }
}

bool Scheduler::should_requeue(
    const Request& request) const
{
    const std::uint64_t remaining = (request.total_bytes > request.offset) ? (request.total_bytes - request.offset) : 0;

    return remaining > 0;
}


SchedulingPolicy Scheduler::policy() const
{
    return policy_;
}


std::uint64_t Scheduler::quantum() const
{
    return quantum_;
}