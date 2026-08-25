#pragma once

#include "n64/common/types.hpp"

#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <vector>

namespace n64 {

/// Named event kinds scheduled on the master clock.
enum class EventKind : u8 {
    VerticalInterrupt = 0,
    HorizontalBlank,
    AiDmaComplete,
    PiDmaComplete,
    SiDmaComplete,
    SpTaskDone,
    DpComplete,
    CountCompare, // COP0 Count/Compare
    User,
};

struct ScheduledEvent {
    Cycles when = 0;
    EventKind kind = EventKind::User;
    u64 token = 0; // opaque user data / cancel id
};

/// Simple priority-queue scheduler. Phase 0: API + unit-testable enqueue/pop.
class Scheduler {
public:
    using Handler = std::function<void(const ScheduledEvent&)>;

    Scheduler() = default;

    void reset();

    /// Current master time in CPU cycles.
    [[nodiscard]] Cycles now() const noexcept { return now_; }

    /// Advance virtual time (does not fire events by itself).
    void advance(Cycles delta);

    /// Schedule an event at an absolute cycle count.
    u64 schedule_at(Cycles when, EventKind kind);

    /// Schedule an event `delay` cycles from now.
    u64 schedule_in(Cycles delay, EventKind kind);

    /// Cancel a previously scheduled event by token. Returns true if found.
    bool cancel(u64 token);

    /// Fire all events with when <= now_. Returns number fired.
    std::size_t dispatch_due(const Handler& handler);

    [[nodiscard]] std::size_t pending() const noexcept { return queue_.size(); }
    [[nodiscard]] bool empty() const noexcept { return queue_.empty(); }

private:
    struct Entry {
        ScheduledEvent ev;
        bool cancelled = false;

        // Min-heap by time.
        bool operator>(const Entry& o) const noexcept {
            if (ev.when != o.ev.when) {
                return ev.when > o.ev.when;
            }
            return ev.token > o.ev.token;
        }
    };

    Cycles now_ = 0;
    u64 next_token_ = 1;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue_;
};

} // namespace n64
