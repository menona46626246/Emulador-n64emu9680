#pragma once

#include "n64/common/types.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace n64 {

class Cpu;
class Bus;
class Emulator;

/// One CPU instruction trace line (ring-buffer entry).
struct TraceEntry {
    u64 cycle = 0;
    u64 pc = 0;
    u32 insn = 0;
    std::string disasm;
};

/// Reason the emulator paused under debugger control.
enum class DebugStopReason : u8 {
    None = 0,
    UserPause,
    Breakpoint,
    Watchpoint,
    StepDone,
    Exception,
};

/// Memory watchpoint.
struct Watchpoint {
    int id = 0;
    u32 address = 0;       // physical (low 29 bits compared)
    bool on_read = false;
    bool on_write = true;
    bool enabled = true;
};

/// Lightweight debugger: breakpoints, watchpoints, instruction ring, MMIO log.
/// Not a full ImGui UI — overlay text + CLI + tests (Phase 10).
class Debugger {
public:
    static constexpr std::size_t kDefaultTraceCapacity = 4096;
    static constexpr std::size_t kMaxBreakpoints = 64;
    static constexpr std::size_t kMaxWatchpoints = 32;

    Debugger();

    void reset();
    void set_enabled(bool on) noexcept { enabled_ = on; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    // ----- Execution control -------------------------------------------------
    void pause(DebugStopReason reason = DebugStopReason::UserPause);
    void resume();
    void request_step(u32 count = 1); // run N instructions then pause

    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] DebugStopReason stop_reason() const noexcept { return stop_reason_; }
    [[nodiscard]] std::string_view stop_reason_name() const noexcept;

    /// Called by Emulator before each CPU step. Returns false → skip step (paused).
    [[nodiscard]] bool on_before_step(Cpu& cpu);

    /// Called after a successful CPU step (for step-count).
    void on_after_step(Cpu& cpu);

    /// Feed a trace line (from CPU set_trace callback).
    void on_trace(u64 cycle, u64 pc, u32 insn, std::string_view disasm);

    /// Called from Bus on every read/write when enabled (watchpoints + MMIO log).
    void on_bus_read(u32 paddr, u32 size);
    void on_bus_write(u32 paddr, u32 size, u64 value);

    // ----- Breakpoints -------------------------------------------------------
    bool add_breakpoint(u64 pc);
    bool remove_breakpoint(u64 pc);
    void clear_breakpoints();
    [[nodiscard]] bool has_breakpoint(u64 pc) const;
    [[nodiscard]] std::vector<u64> breakpoints() const;

    // ----- Watchpoints -------------------------------------------------------
    int add_watchpoint(u32 paddr, bool on_read, bool on_write);
    bool remove_watchpoint(int id);
    void clear_watchpoints();
    [[nodiscard]] std::vector<Watchpoint> watchpoints() const;

    // ----- Trace ring --------------------------------------------------------
    void set_trace_capacity(std::size_t n);
    void set_trace_enabled(bool on) noexcept { trace_enabled_ = on; }
    [[nodiscard]] bool trace_enabled() const noexcept { return trace_enabled_; }
    void clear_trace();
    [[nodiscard]] std::vector<TraceEntry> recent_trace(std::size_t max_n = 32) const;
    [[nodiscard]] std::size_t trace_size() const;

    /// Dump last N trace lines to a string (for file / overlay).
    [[nodiscard]] std::string format_trace(std::size_t max_n = 16) const;

    // ----- MMIO / unknown access log -----------------------------------------
    void log_unknown_mmio(u32 paddr, bool is_write, u32 value = 0);
    [[nodiscard]] std::size_t unknown_mmio_count() const;
    [[nodiscard]] std::string format_unknown_mmio(std::size_t max_n = 16) const;

    // ----- Snapshot / overlay ------------------------------------------------
    /// Multi-line status for on-screen overlay (PC, GPRs subset, stops, etc.).
    [[nodiscard]] std::string format_overlay(const Emulator& emu) const;

    /// Compact one-line status for window title.
    [[nodiscard]] std::string format_status_line(const Emulator& emu) const;

    /// Attach CPU trace callback to this debugger (call after boot).
    void attach_cpu_trace(Cpu& cpu);

    [[nodiscard]] u64 hit_count() const noexcept { return hit_count_; }
    [[nodiscard]] u64 watch_hit_count() const noexcept { return watch_hit_count_; }

private:
    [[nodiscard]] static u32 phys29(u32 a) noexcept { return a & 0x1FFF'FFFFu; }

    bool enabled_ = false;
    bool paused_ = false;
    bool trace_enabled_ = false;
    DebugStopReason stop_reason_ = DebugStopReason::None;

    u32 steps_left_ = 0; // >0 means single-stepping
    std::optional<u64> resume_breakpoint_pc_;
    u64 hit_count_ = 0;
    u64 watch_hit_count_ = 0;

    std::unordered_set<u64> breakpoints_;
    std::vector<Watchpoint> watchpoints_;
    int next_watch_id_ = 1;

    mutable std::mutex trace_mu_;
    std::deque<TraceEntry> trace_;
    std::size_t trace_capacity_ = kDefaultTraceCapacity;

    struct MmioHit {
        u32 paddr = 0;
        bool is_write = false;
        u32 value = 0;
        u64 count = 1;
    };
    mutable std::mutex mmio_mu_;
    std::unordered_map<u32, MmioHit> unknown_mmio_; // key = paddr | (write<<31)
    std::vector<u32> unknown_mmio_order_;
};

[[nodiscard]] const char* debug_stop_reason_cstr(DebugStopReason r) noexcept;

} // namespace n64
