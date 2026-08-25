#pragma once

#include "n64/cart/header.hpp"
#include "n64/common/types.hpp"
#include "n64/core/timing.hpp"
#include "n64/debug/debugger.hpp"
#include "n64/pif/boot.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace n64 {

class Cpu;
class Bus;
class VideoInterface;
class AudioInterface;
class Pif;
class Rsp;
class Rdp;
class Scheduler;

/// Top-level emulator facade. Coordinates CPU, bus, RCP, VI/AI, boot and timing.
class Emulator {
public:
    Emulator();
    ~Emulator();

    Emulator(const Emulator&) = delete;
    Emulator& operator=(const Emulator&) = delete;
    Emulator(Emulator&&) noexcept;
    Emulator& operator=(Emulator&&) noexcept;

    /// Soft-reset hardware state. If a ROM/PIF is loaded, re-runs last boot config.
    void reset();

    /// Default / last-used boot configuration (PIF ROM path, CIC force, etc.).
    void set_boot_config(const BootConfig& cfg) { boot_cfg_ = cfg; }
    [[nodiscard]] const BootConfig& boot_config() const noexcept { return boot_cfg_; }

    /// Master timing (NTSC/PAL budgets, RSP ratio, VI sync).
    void set_timing(const TimingConfig& t) { timing_ = t; }
    [[nodiscard]] const TimingConfig& timing() const noexcept { return timing_; }

    /// Load a cartridge image from disk and boot (uses boot_cfg_).
    [[nodiscard]] bool load_rom(std::string_view path);

    /// Load cartridge bytes from memory and boot (tests / tools).
    [[nodiscard]] bool load_rom_bytes(std::span<const u8> data,
                                      const BootConfig& boot = {});

    /// Re-run boot with an explicit config (also becomes the new default).
    [[nodiscard]] bool boot(const BootConfig& cfg);

    /// Re-run boot with the stored boot_cfg_.
    [[nodiscard]] bool boot() { return boot(boot_cfg_); }

    /// Advance emulation by approximately `cycles` VR4300 cycles.
    /// Uses batched device ticks (Phase 9) for better throughput.
    void run_cycles(Cycles cycles);

    /// Run until the next VI frame boundary (or max budget). Preferred for UI.
    FrameResult run_frame();

    /// Run until stop, or `max_cycles` is reached (0 = unlimited).
    void run(Cycles max_cycles = 0);

    void request_stop() noexcept { stop_requested_ = true; }
    [[nodiscard]] bool stop_requested() const noexcept { return stop_requested_; }

    [[nodiscard]] bool rom_loaded() const noexcept { return rom_loaded_; }
    [[nodiscard]] bool booted() const noexcept { return booted_; }
    [[nodiscard]] Cycles cycles_executed() const noexcept { return cycles_executed_; }
    [[nodiscard]] Cycles master_cycles() const noexcept;
    [[nodiscard]] const std::string& rom_path() const noexcept { return rom_path_; }
    [[nodiscard]] const CartHeader& cart_header() const noexcept { return header_; }
    [[nodiscard]] u32 entrypoint() const noexcept { return header_.entrypoint; }

    /// True if VI completed a frame since the last `clear_frame_ready()`.
    [[nodiscard]] bool frame_ready() const noexcept;
    void clear_frame_ready() noexcept;

    /// Copy current VI framebuffer to tightly packed RGBA8888 bytes.
    [[nodiscard]] bool present_framebuffer(std::vector<u8>& rgba,
                                           int& width,
                                           int& height) const;

    [[nodiscard]] u64 vi_frame_count() const noexcept;

    // ----- Debugger (Phase 10) -----------------------------------------------
    [[nodiscard]] Debugger& debugger() noexcept { return *debugger_; }
    [[nodiscard]] const Debugger& debugger() const noexcept { return *debugger_; }
    void enable_debugger(bool on);
    /// Dump recent CPU trace to a text file. Returns false on I/O error.
    [[nodiscard]] bool dump_trace_file(std::string_view path, std::size_t max_n = 1024) const;

    // Component accessors (non-owning). Valid for the lifetime of Emulator.
    [[nodiscard]] Cpu& cpu() noexcept;
    [[nodiscard]] const Cpu& cpu() const noexcept;
    [[nodiscard]] Bus& bus() noexcept;
    [[nodiscard]] const Bus& bus() const noexcept;
    [[nodiscard]] VideoInterface& vi() noexcept;
    [[nodiscard]] const VideoInterface& vi() const noexcept;
    [[nodiscard]] AudioInterface& ai() noexcept;
    [[nodiscard]] const AudioInterface& ai() const noexcept;
    [[nodiscard]] Pif& pif() noexcept;
    [[nodiscard]] const Pif& pif() const noexcept;
    [[nodiscard]] Rsp& rsp() noexcept;
    [[nodiscard]] const Rsp& rsp() const noexcept;
    [[nodiscard]] Rdp& rdp() noexcept;
    [[nodiscard]] const Rdp& rdp() const noexcept;
    [[nodiscard]] Scheduler& scheduler() noexcept;
    [[nodiscard]] const Scheduler& scheduler() const noexcept;

private:
    /// Execute up to `max_cpu_steps` plus proportional RSP work. Debug mode
    /// remains instruction-granular. Returns 0 if the debugger paused execution.
    Cycles step_cpu_rsp(Cycles max_cpu_steps = 1);

    /// Advance VI, AI and scheduler by `delta` master cycles and dispatch events.
    void advance_devices(Cycles delta);

    struct Impl;
    std::unique_ptr<Impl> impl_;

    bool rom_loaded_ = false;
    bool booted_ = false;
    bool stop_requested_ = false;
    Cycles cycles_executed_ = 0;
    std::string rom_path_;
    CartHeader header_{};
    BootConfig boot_cfg_{};
    TimingConfig timing_ = TimingConfig::ntsc();
    std::unique_ptr<Debugger> debugger_;
};

} // namespace n64
