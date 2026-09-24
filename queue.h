#ifndef QUEUE_H
#define QUEUE_H

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <iterator>

enum class Operation {
    GET,
    PUT,
    HEALTH
};

struct Request {
    int request_id = 0;
    int client_fd = -1;

    Operation op = Operation::GET;
    std::string filename;

    uint64_t total_bytes = 0;

    uint64_t offset = 0;

    uint64_t rounds = 0;
    uint64_t forfeited_bytes = 0;
    uint64_t deficit = 0;

    uint64_t arrival_ns = 0;
    uint64_t start_ns = 0;
    uint64_t finish_ns = 0;

    std::vector<char> pending_bytes;
    std::size_t pending_offset = 0;
    bool response_sent = false;
};

class RequestQueue {
public:
    RequestQueue() = default;
    ~RequestQueue() = default;

    bool push(Request request);

    bool wait_pop(Request& request);

    bool try_pop(Request& request);

    template <typename Compare>
    bool wait_pop_best(Request& request, Compare compare);

    bool requeue(Request request);

    std::size_t size() const;

    bool empty() const;

    void close();

    bool is_closed() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::deque<Request> queue_;

    bool closed_ = false;
};


template <typename Compare>
bool RequestQueue::wait_pop_best(Request& request, Compare compare)
{
    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait(lock, [this]() {
        return !queue_.empty() || closed_;
    });

    if (queue_.empty()) {
        return false;
    }

    auto best_it = queue_.begin();

    for (auto it = std::next(queue_.begin()); it != queue_.end(); ++it) {
        if (compare(*it, *best_it)) {
            best_it = it;
        }
    }

    request = std::move(*best_it);
    queue_.erase(best_it);

    return true;
}

#endif