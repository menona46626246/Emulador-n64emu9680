#pragma once

#include "n64/common/types.hpp"

namespace n64 {

/// Master-clock / region timing constants (public NTSC/PAL approximations).
/// INCÓGNITA U001: exact VR4300↔RCP divisors — refine with experiments later.
struct TimingConfig {
    /// Target display / VI field rate.
    double target_fps = 60.0;

    /// CPU cycles budgeted per VI frame at target_fps.
    /// Default = kCpuClockHz / 60 ≈ 1_562_500.
    Cycles cycles_per_frame = 0;

    /// Max CPU cycles to run in one run_frame() slice before yielding
    /// even if VI has not completed (safety against hang).
    Cycles max_cycles_per_frame = 0;

    /// RSP scalar steps executed per CPU instruction while unhalted.
    u32 rsp_steps_per_cpu = 4;

    /// When true, run_frame stops at the first VI frame boundary.
    bool sync_to_vi = true;

    static TimingConfig ntsc() {
        TimingConfig t;
        t.target_fps = 60.0;
        t.cycles_per_frame = kCpuClockHz / 60;          // 1_562_500
        t.max_cycles_per_frame = t.cycles_per_frame * 2;
        t.rsp_steps_per_cpu = 4;
        t.sync_to_vi = true;
        return t;
    }

    static TimingConfig pal() {
        TimingConfig t;
        t.target_fps = 50.0;
        t.cycles_per_frame = kCpuClockHz / 50;          // 1_875_000
        t.max_cycles_per_frame = t.cycles_per_frame * 2;
        t.rsp_steps_per_cpu = 4;
        t.sync_to_vi = true;
        return t;
    }
};

/// Result of one run_frame() invocation.
struct FrameResult {
    Cycles cycles_ran = 0;
    bool vi_frame = false;
    u64 vi_frame_index = 0;
    bool hit_limit = false;   // stopped due to max_cycles, not VI
    bool stopped = false;     // stop requested or debugger paused
};

} // namespace n64
