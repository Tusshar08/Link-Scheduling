#ifndef SCHEDULING_H
#define SCHEDULING_H

#include "queue.h"

#include <cstddef>
#include <cstdint>

enum class SchedulingPolicy {
    FCFS,
    SJF,
    RR,
    DRR
};

struct ScheduleDecision {
    Request request;
    std::uint64_t budget = 0;
    bool valid = false;
};

class Scheduler {
public:
    Scheduler(SchedulingPolicy policy, std::uint64_t quantum = 0);

    ScheduleDecision next(RequestQueue& queue);

    std::uint64_t get_budget(Request& request);

    void account(Request& request, std::uint64_t bytes_processed);

    bool should_requeue(const Request& request) const;

    SchedulingPolicy policy() const;

    std::uint64_t quantum() const;

private:
    SchedulingPolicy policy_;
    std::uint64_t quantum_;
};

#endif