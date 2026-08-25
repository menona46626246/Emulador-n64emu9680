#include "n64/core/scheduler.hpp"

namespace n64 {

void Scheduler::reset() {
    now_ = 0;
    next_token_ = 1;
    // priority_queue has no clear — swap with empty.
    decltype(queue_) empty;
    queue_.swap(empty);
}

void Scheduler::advance(Cycles delta) {
    now_ += delta;
}

u64 Scheduler::schedule_at(Cycles when, EventKind kind) {
    ScheduledEvent ev;
    ev.when = when;
    ev.kind = kind;
    ev.token = next_token_++;
    queue_.push(Entry{ev, false});
    return ev.token;
}

u64 Scheduler::schedule_in(Cycles delay, EventKind kind) {
    return schedule_at(now_ + delay, kind);
}

bool Scheduler::cancel(u64 token) {
    // Lazy cancel: mark matching entries; they are dropped on dispatch/pop.
    // Since priority_queue doesn't allow in-place mutation, we rebuild.
    if (queue_.empty()) {
        return false;
    }
    std::vector<Entry> kept;
    kept.reserve(queue_.size());
    bool found = false;
    while (!queue_.empty()) {
        Entry e = queue_.top();
        queue_.pop();
        if (e.ev.token == token && !e.cancelled) {
            found = true;
            // drop
        } else {
            kept.push_back(e);
        }
    }
    for (auto& e : kept) {
        queue_.push(std::move(e));
    }
    return found;
}

std::size_t Scheduler::dispatch_due(const Handler& handler) {
    std::size_t fired = 0;
    while (!queue_.empty()) {
        Entry e = queue_.top();
        if (e.ev.when > now_) {
            break;
        }
        queue_.pop();
        if (e.cancelled) {
            continue;
        }
        if (handler) {
            handler(e.ev);
        }
        ++fired;
    }
    return fired;
}

} // namespace n64
