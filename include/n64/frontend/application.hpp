#pragma once

#include "n64/core/emulator.hpp"
#include "n64/frontend/audio_output.hpp"
#include "n64/frontend/input.hpp"
#include "n64/frontend/window.hpp"

#include <spdlog/common.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace n64::frontend {

struct AppConfig {
    WindowConfig window;
    std::string rom_path;
    bool pause_on_start = false;
    /// Keep the window open this many milliseconds (0 = until user closes).
    int auto_close_ms = 0;
    bool mute = false;
    /// Pace the UI loop to the emulator target fps (VI-synced run_frame).
    bool pace_to_fps = true;
    /// Enable debugger + overlay (Phase 10).
    bool debug = false;
    /// Start with CPU instruction trace ring enabled.
    bool trace = false;
    /// Optional path to dump trace on exit.
    std::string trace_dump_path;
    spdlog::level::level_enum log_level = spdlog::level::info;

    /// Boot options (Phase 3). PIF/IPL paths must be user-owned legal files.
    n64::BootConfig boot;

    /// PC breakpoints to install when --debug is on (virtual addresses).
    std::vector<u64> break_pcs;
};

/// Owns the emulator + window + input + audio and runs the main loop.
class Application {
public:
    Application() = default;

    /// Parse argv into config. Recognizes:
    ///   --rom PATH, --headless, --auto-close MS, --mute, --log-level LEVEL, --help
    [[nodiscard]] static AppConfig parse_args(int argc, char** argv);

    [[nodiscard]] int run(const AppConfig& cfg);

private:
    Emulator emulator_;
    Window window_;
    InputMapper input_;
    AudioOutput audio_;
};

} // namespace n64::frontend
