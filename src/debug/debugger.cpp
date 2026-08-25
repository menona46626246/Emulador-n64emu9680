#include "n64/debug/debugger.hpp"

#include "n64/ai/ai.hpp"
#include "n64/bus/bus.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/rcp/rdp/rdp.hpp"
#include "n64/rcp/rsp/rsp.hpp"
#include "n64/vi/vi.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace n64 {

const char* debug_stop_reason_cstr(DebugStopReason r) noexcept {
    switch (r) {
    case DebugStopReason::None:        return "running";
    case DebugStopReason::UserPause:   return "paused";
    case DebugStopReason::Breakpoint:  return "breakpoint";
    case DebugStopReason::Watchpoint:  return "watchpoint";
    case DebugStopReason::StepDone:    return "step";
    case DebugStopReason::Exception:   return "exception";
    default:                           return "unknown";
    }
}

Debugger::Debugger() = default;

void Debugger::reset() {
    paused_ = false;
    stop_reason_ = DebugStopReason::None;
    steps_left_ = 0;
    resume_breakpoint_pc_.reset();
    hit_count_ = 0;
    watch_hit_count_ = 0;
    // Keep breakpoints/watchpoints across soft reset — only clear trace/mmio.
    clear_trace();
    {
        std::lock_guard lock(mmio_mu_);
        unknown_mmio_.clear();
        unknown_mmio_order_.clear();
    }
}

void Debugger::pause(DebugStopReason reason) {
    paused_ = true;
    stop_reason_ = reason;
    steps_left_ = 0;
}

void Debugger::resume() {
    paused_ = false;
    stop_reason_ = DebugStopReason::None;
    steps_left_ = 0;
}

void Debugger::request_step(u32 count) {
    if (count == 0) {
        count = 1;
    }
    steps_left_ = count;
    paused_ = false;
    stop_reason_ = DebugStopReason::None;
}

std::string_view Debugger::stop_reason_name() const noexcept {
    return debug_stop_reason_cstr(stop_reason_);
}

bool Debugger::on_before_step(Cpu& cpu) {
    if (!enabled_) {
        return true;
    }
    if (paused_ && steps_left_ == 0) {
        return false;
    }

    const u64 pc = cpu.pc();
    if (resume_breakpoint_pc_) {
        const bool skip = *resume_breakpoint_pc_ == pc;
        resume_breakpoint_pc_.reset();
        if (skip) {
            return true;
        }
    }
    if (!breakpoints_.empty() && breakpoints_.count(pc) != 0) {
        ++hit_count_;
        resume_breakpoint_pc_ = pc;
        pause(DebugStopReason::Breakpoint);
        return false;
    }
    return true;
}

void Debugger::on_after_step(Cpu& /*cpu*/) {
    if (!enabled_) {
        return;
    }
    if (steps_left_ > 0) {
        --steps_left_;
        if (steps_left_ == 0) {
            pause(DebugStopReason::StepDone);
        }
    }
}

void Debugger::on_trace(u64 cycle, u64 pc, u32 insn, std::string_view disasm) {
    if (!trace_enabled_) {
        return;
    }
    std::lock_guard lock(trace_mu_);
    if (trace_.size() >= trace_capacity_) {
        trace_.pop_front();
    }
    TraceEntry e;
    e.cycle = cycle;
    e.pc = pc;
    e.insn = insn;
    e.disasm.assign(disasm);
    trace_.push_back(std::move(e));
}

void Debugger::on_bus_read(u32 paddr, u32 size) {
    if (!enabled_ || watchpoints_.empty()) {
        return;
    }
    const u32 p = phys29(paddr);
    for (const auto& w : watchpoints_) {
        if (!w.enabled || !w.on_read) continue;
        bool overlaps = false;
        for (u32 i = 0; i < std::max(1u, size); ++i) {
            if (phys29(p + i) == phys29(w.address)) {
                overlaps = true;
                break;
            }
        }
        if (overlaps) {
            ++watch_hit_count_;
            pause(DebugStopReason::Watchpoint);
            return;
        }
    }
}

void Debugger::on_bus_write(u32 paddr, u32 size, u64 /*value*/) {
    if (!enabled_ || watchpoints_.empty()) {
        return;
    }
    const u32 p = phys29(paddr);
    for (const auto& w : watchpoints_) {
        if (!w.enabled || !w.on_write) continue;
        bool overlaps = false;
        for (u32 i = 0; i < std::max(1u, size); ++i) {
            if (phys29(p + i) == phys29(w.address)) {
                overlaps = true;
                break;
            }
        }
        if (overlaps) {
            ++watch_hit_count_;
            pause(DebugStopReason::Watchpoint);
            return;
        }
    }
}

bool Debugger::add_breakpoint(u64 pc) {
    if (breakpoints_.size() >= kMaxBreakpoints) {
        return false;
    }
    breakpoints_.insert(pc);
    return true;
}

bool Debugger::remove_breakpoint(u64 pc) {
    return breakpoints_.erase(pc) > 0;
}

void Debugger::clear_breakpoints() {
    breakpoints_.clear();
}

bool Debugger::has_breakpoint(u64 pc) const {
    return breakpoints_.count(pc) != 0;
}

std::vector<u64> Debugger::breakpoints() const {
    std::vector<u64> out;
    out.reserve(breakpoints_.size());
    for (u64 p : breakpoints_) {
        out.push_back(p);
    }
    std::sort(out.begin(), out.end());
    return out;
}

int Debugger::add_watchpoint(u32 paddr, bool on_read, bool on_write) {
    if (watchpoints_.size() >= kMaxWatchpoints) {
        return -1;
    }
    if (!on_read && !on_write) {
        on_write = true;
    }
    Watchpoint w;
    w.id = next_watch_id_++;
    w.address = phys29(paddr);
    w.on_read = on_read;
    w.on_write = on_write;
    w.enabled = true;
    watchpoints_.push_back(w);
    return w.id;
}

bool Debugger::remove_watchpoint(int id) {
    const auto it = std::find_if(watchpoints_.begin(), watchpoints_.end(),
                                 [id](const Watchpoint& w) { return w.id == id; });
    if (it == watchpoints_.end()) {
        return false;
    }
    watchpoints_.erase(it);
    return true;
}

void Debugger::clear_watchpoints() {
    watchpoints_.clear();
}

std::vector<Watchpoint> Debugger::watchpoints() const {
    return watchpoints_;
}

void Debugger::set_trace_capacity(std::size_t n) {
    if (n < 16) n = 16;
    if (n > 1'000'000) n = 1'000'000;
    std::lock_guard lock(trace_mu_);
    trace_capacity_ = n;
    while (trace_.size() > trace_capacity_) {
        trace_.pop_front();
    }
}

void Debugger::clear_trace() {
    std::lock_guard lock(trace_mu_);
    trace_.clear();
}

std::vector<TraceEntry> Debugger::recent_trace(std::size_t max_n) const {
    std::lock_guard lock(trace_mu_);
    std::vector<TraceEntry> out;
    if (trace_.empty() || max_n == 0) {
        return out;
    }
    const std::size_t n = std::min(max_n, trace_.size());
    out.reserve(n);
    const auto start = trace_.size() - n;
    for (std::size_t i = start; i < trace_.size(); ++i) {
        out.push_back(trace_[i]);
    }
    return out;
}

std::size_t Debugger::trace_size() const {
    std::lock_guard lock(trace_mu_);
    return trace_.size();
}

std::string Debugger::format_trace(std::size_t max_n) const {
    const auto lines = recent_trace(max_n);
    std::ostringstream oss;
    for (const auto& e : lines) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%10llu  %08X  %08X  %s\n",
                      static_cast<unsigned long long>(e.cycle),
                      static_cast<u32>(e.pc),
                      e.insn,
                      e.disasm.c_str());
        oss << buf;
    }
    return oss.str();
}

void Debugger::log_unknown_mmio(u32 paddr, bool is_write, u32 value) {
    const u32 key = phys29(paddr) | (is_write ? 0x8000'0000u : 0u);
    std::lock_guard lock(mmio_mu_);
    auto it = unknown_mmio_.find(key);
    if (it == unknown_mmio_.end()) {
        MmioHit h;
        h.paddr = phys29(paddr);
        h.is_write = is_write;
        h.value = value;
        h.count = 1;
        unknown_mmio_.emplace(key, h);
        unknown_mmio_order_.push_back(key);
    } else {
        ++it->second.count;
        it->second.value = value;
    }
}

std::size_t Debugger::unknown_mmio_count() const {
    std::lock_guard lock(mmio_mu_);
    return unknown_mmio_.size();
}

std::string Debugger::format_unknown_mmio(std::size_t max_n) const {
    std::lock_guard lock(mmio_mu_);
    std::ostringstream oss;
    std::size_t n = 0;
    for (auto it = unknown_mmio_order_.rbegin();
         it != unknown_mmio_order_.rend() && n < max_n; ++it, ++n) {
        const auto& h = unknown_mmio_.at(*it);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s %08X val=%08X x%llu\n",
                      h.is_write ? "W" : "R",
                      h.paddr, h.value,
                      static_cast<unsigned long long>(h.count));
        oss << buf;
    }
    return oss.str();
}

void Debugger::attach_cpu_trace(Cpu& cpu) {
    if (!enabled_) {
        cpu.clear_trace();
        return;
    }
    // Always install callback when debugger is on; on_trace no-ops if
    // trace_enabled_ is false (allows toggling without re-attach).
    cpu.set_trace([this, &cpu](u64 pc, u32 insn, const std::string& disasm) {
        on_trace(cpu.cycles(), pc, insn, disasm);
    });
}

std::string Debugger::format_overlay(const Emulator& emu) const {
    std::ostringstream oss;
    const auto& cpu = emu.cpu();
    char line[128];

    std::snprintf(line, sizeof(line),
                  "n64emu DEBUG [%s]  reason=%s  bp=%zu wp=%zu\n",
                  enabled_ ? (paused_ ? "PAUSED" : "RUN") : "off",
                  debug_stop_reason_cstr(stop_reason_),
                  breakpoints_.size(), watchpoints_.size());
    oss << line;

    std::snprintf(line, sizeof(line),
                  "PC=%08X  cycles=%llu  exc=%llu  vi=%llu  ai_dma=%llu\n",
                  static_cast<u32>(cpu.pc()),
                  static_cast<unsigned long long>(cpu.cycles()),
                  static_cast<unsigned long long>(cpu.exception_count()),
                  static_cast<unsigned long long>(emu.vi_frame_count()),
                  static_cast<unsigned long long>(emu.ai().dmas_completed()));
    oss << line;

    std::snprintf(line, sizeof(line),
                  "SR=%08X CAUSE=%08X EPC=%08X HI=%08X LO=%08X\n",
                  cpu.cop0(Cop0Reg::Status),
                  cpu.cop0(Cop0Reg::Cause),
                  cpu.cop0(Cop0Reg::EPC),
                  static_cast<u32>(cpu.hi()),
                  static_cast<u32>(cpu.lo()));
    oss << line;

    // GPRs r1-r16 compact
    for (int row = 0; row < 4; ++row) {
        oss << " ";
        for (int col = 0; col < 4; ++col) {
            const int r = row * 4 + col + 1;
            std::snprintf(line, sizeof(line), "r%-2d=%08X ", r,
                          static_cast<u32>(cpu.gpr(static_cast<std::size_t>(r))));
            oss << line;
        }
        oss << "\n";
    }

    std::snprintf(line, sizeof(line),
                  "RSP %s pc=%03X  RDP cmds=%llu fill=%llu tex=%llu\n",
                  emu.rsp().halted() ? "HALT" : "RUN ",
                  emu.rsp().pc(),
                  static_cast<unsigned long long>(emu.rdp().commands_executed()),
                  static_cast<unsigned long long>(emu.rdp().fill_pixels()),
                  static_cast<unsigned long long>(emu.rdp().tex_pixels()));
    oss << line;

    // Keys hint
    oss << " F1 overlay  F2 pause  F3 step  F5 resume  F6 trace\n";

    if (trace_enabled_) {
        oss << "--- last trace ---\n";
        oss << format_trace(8);
    }
    return oss.str();
}

std::string Debugger::format_status_line(const Emulator& emu) const {
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "PC=%08X %s bp=%zu vi=%llu",
                  static_cast<u32>(emu.cpu().pc()),
                  enabled_ ? (paused_ ? "PAUSE" : "RUN") : "dbg-off",
                  breakpoints_.size(),
                  static_cast<unsigned long long>(emu.vi_frame_count()));
    return std::string(buf);
}

} // namespace n64
