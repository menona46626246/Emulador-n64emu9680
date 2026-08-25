#include "n64/core/emulator.hpp"

#include "n64/ai/ai.hpp"
#include "n64/bus/bus.hpp"
#include "n64/common/log.hpp"
#include "n64/core/scheduler.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/cpu/block_cache.hpp"
#include "n64/pif/boot.hpp"
#include "n64/pif/pif.hpp"
#include "n64/rcp/rdp/rdp.hpp"
#include "n64/rcp/rsp/rsp.hpp"
#include "n64/vi/vi.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

namespace n64 {
namespace {

// How often (in CPU steps) we flush accumulated device ticks.
// Larger = faster; smaller = tighter IRQ latency. 32 is a good balance.
constexpr Cycles kDeviceBatch = 32;

} // namespace

struct Emulator::Impl {
    Bus bus;
    Cpu cpu;
    BlockCache block_cache;
    VideoInterface vi;
    AudioInterface ai;
    Pif pif;
    Rsp rsp;
    Rdp rdp;
    Scheduler scheduler;

    // Pending device cycles not yet applied (batching).
    Cycles device_debt = 0;

    Impl() = default;

    void wire(Debugger* dbg) {
        cpu.connect_bus(&bus);
        cpu.set_block_cache(&block_cache);
        bus.connect(&cpu, &vi, &ai, &pif, &rsp, &rdp);
        bus.set_debugger(dbg);
    }

    void flush_devices() {
        if (device_debt == 0) {
            return;
        }
        const Cycles d = device_debt;
        device_debt = 0;
        vi.tick(d);
        ai.tick(d);
        scheduler.advance(d);
        scheduler.dispatch_due([](const ScheduledEvent&) {
            // Future: wire timed DMA completions / VI callbacks here.
        });
    }
};

Emulator::Emulator()
    : impl_(std::make_unique<Impl>())
    , debugger_(std::make_unique<Debugger>()) {
    timing_ = TimingConfig::ntsc();
    impl_->wire(debugger_.get());
    reset();
}

Emulator::~Emulator() = default;

Emulator::Emulator(Emulator&&) noexcept = default;
Emulator& Emulator::operator=(Emulator&&) noexcept = default;

void Emulator::reset() {
    stop_requested_ = false;
    cycles_executed_ = 0;
    booted_ = false;
    impl_->device_debt = 0;
    impl_->bus.reset();
    impl_->cpu.reset();
    impl_->vi.reset();
    impl_->ai.reset();
    impl_->pif.reset();
    impl_->rsp.reset();
    impl_->rdp.reset();
    impl_->scheduler.reset();
    impl_->block_cache.clear();
    impl_->wire(debugger_.get());
    debugger_->reset();

    const bool can_boot = impl_->bus.has_cartridge() ||
                          (boot_cfg_.mode == BootMode::LlePif &&
                           (!boot_cfg_.pif_rom_path.empty() || impl_->bus.has_pif_rom()));
    if (can_boot) {
        BootConfig cfg = boot_cfg_;
        cfg.reset_type = 1;
        if (!boot(cfg)) {
            N64_WARN("reset: boot failed after soft reset");
        }
    }
    N64_DEBUG("Emulator reset");
}

bool Emulator::boot(const BootConfig& cfg) {
    boot_cfg_ = cfg;
    impl_->block_cache.clear();
    const BootResult r = hle_boot_loaded(impl_->bus, impl_->cpu, impl_->pif, impl_->rsp, cfg);
    if (!r.ok) {
        booted_ = false;
        header_ = {};
        N64_ERROR("boot failed: {}", r.error ? r.error : "unknown");
        return false;
    }
    header_ = r.header;
    booted_ = true;
    cycles_executed_ = 0;
    stop_requested_ = false;
    impl_->device_debt = 0;

    // Match timing region to cart TV type when possible.
    if (header_.tv == TvType::PAL) {
        timing_ = TimingConfig::pal();
    } else {
        timing_ = TimingConfig::ntsc();
    }
    // Align VI line timing with cycles_per_frame / lines.
    // VI keeps its own cycles_per_line; ensure frame budget is consistent.
    if (timing_.cycles_per_frame == 0) {
        timing_.cycles_per_frame = kCpuClockHz / 60;
    }
    if (timing_.max_cycles_per_frame == 0) {
        timing_.max_cycles_per_frame = timing_.cycles_per_frame * 2;
    }

    N64_INFO("Timing: {:.2f} fps, {} cycles/frame, RSP x{}",
             timing_.target_fps, timing_.cycles_per_frame, timing_.rsp_steps_per_cpu);

    if (debugger_->enabled()) {
        debugger_->attach_cpu_trace(impl_->cpu);
    }
    return true;
}

bool Emulator::load_rom(std::string_view path) {
    stop_requested_ = false;
    cycles_executed_ = 0;
    booted_ = false;
    impl_->device_debt = 0;
    impl_->bus.reset();
    impl_->cpu.reset();
    impl_->vi.reset();
    impl_->ai.reset();
    impl_->pif.reset();
    impl_->rsp.reset();
    impl_->rdp.reset();
    impl_->scheduler.reset();
    impl_->block_cache.clear();
    impl_->wire(debugger_.get());

    if (!impl_->bus.load_cartridge_file(path)) {
        N64_ERROR("Failed to load ROM: {}", path);
        rom_loaded_ = false;
        rom_path_.clear();
        header_ = {};
        return false;
    }
    rom_loaded_ = true;
    rom_path_ = std::string(path);
    N64_INFO("Loaded ROM ({} bytes): {}", impl_->bus.cartridge().size(), path);

    BootConfig cfg = boot_cfg_;
    cfg.reset_type = 0;
    return boot(cfg);
}

bool Emulator::load_rom_bytes(std::span<const u8> data, const BootConfig& boot_arg) {
    stop_requested_ = false;
    cycles_executed_ = 0;
    booted_ = false;
    impl_->device_debt = 0;
    impl_->bus.reset();
    impl_->cpu.reset();
    impl_->vi.reset();
    impl_->ai.reset();
    impl_->pif.reset();
    impl_->rsp.reset();
    impl_->rdp.reset();
    impl_->scheduler.reset();
    impl_->block_cache.clear();
    impl_->wire(debugger_.get());

    if (!impl_->bus.load_cartridge(data)) {
        rom_loaded_ = false;
        rom_path_.clear();
        header_ = {};
        return false;
    }
    rom_loaded_ = true;
    rom_path_.clear();

    BootConfig cfg = boot_cfg_;
    const bool customized =
        boot_arg.skip_rom_copy || boot_arg.force_cic != CicType::Unknown ||
        !boot_arg.pif_rom_path.empty() || !boot_arg.ipl3_path.empty() ||
        boot_arg.force_tv_set || boot_arg.load_size != 0 ||
        boot_arg.mode != BootMode::Hle || boot_arg.reset_type != 0;
    if (customized) {
        cfg = boot_arg;
    }
    return this->boot(cfg);
}

Cycles Emulator::step_cpu_rsp(Cycles max_cpu_steps) {
    if (max_cpu_steps == 0) {
        return 0;
    }
    Cycles cpu_steps = 0;
    if (debugger_->enabled()) {
        if (!debugger_->on_before_step(impl_->cpu)) {
            // Paused: do not advance CPU/RSP/devices.
            return 0;
        }
        impl_->cpu.step();
        debugger_->on_after_step(impl_->cpu);
        cpu_steps = 1;
    } else {
        cpu_steps = impl_->cpu.run(max_cpu_steps);
        if (cpu_steps == 0) {
            // Preserve the old timing behavior for an explicitly halted CPU.
            impl_->cpu.step();
            cpu_steps = 1;
        }
    }

    if (!impl_->rsp.halted()) {
        impl_->rsp.run(static_cast<u32>(cpu_steps * timing_.rsp_steps_per_cpu));
    }
    impl_->device_debt += cpu_steps;
    cycles_executed_ += cpu_steps;
    return cpu_steps;
}

void Emulator::enable_debugger(bool on) {
    debugger_->set_enabled(on);
    if (on) {
        debugger_->attach_cpu_trace(impl_->cpu);
        N64_INFO("Debugger enabled");
    } else {
        impl_->cpu.clear_trace();
        debugger_->resume();
        N64_INFO("Debugger disabled");
    }
}

bool Emulator::dump_trace_file(std::string_view path, std::size_t max_n) const {
    std::ofstream out(std::string(path), std::ios::binary);
    if (!out) {
        return false;
    }
    out << "# n64emu CPU trace\n";
    out << "# cycle  pc  insn  disasm\n";
    out << debugger_->format_trace(max_n);
    return static_cast<bool>(out);
}

void Emulator::advance_devices(Cycles delta) {
    if (delta == 0) {
        return;
    }
    impl_->device_debt += delta;
    // Always flush explicit advances immediately (used rarely).
    impl_->flush_devices();
}

void Emulator::run_cycles(Cycles cycles) {
    if (cycles == 0 || stop_requested_) {
        return;
    }

    Cycles left = cycles;
    while (left > 0 && !stop_requested_) {
        const Cycles chunk = std::min(left, kDeviceBatch);
        const Cycles did = step_cpu_rsp(chunk);
        if (did == 0) {
            break;
        }
        left -= did;
        if (impl_->device_debt >= kDeviceBatch) {
            impl_->flush_devices();
        }
    }
    impl_->flush_devices();

    if (!impl_->rsp.halted() || impl_->rsp.broke()) {
        impl_->rsp.push_mem_to_bus();
    }
}

FrameResult Emulator::run_frame() {
    FrameResult fr{};
    if (stop_requested_) {
        fr.stopped = true;
        return fr;
    }

    // Ensure clean slate for detecting a *new* VI frame this call.
    const u64 frames_before = impl_->vi.frame_count();
    impl_->vi.clear_frame_ready();

    Cycles budget = timing_.cycles_per_frame;
    if (budget == 0) {
        budget = kCpuClockHz / 60;
    }
    Cycles limit = timing_.max_cycles_per_frame;
    if (limit == 0) {
        limit = budget * 2;
    }

    Cycles ran = 0;
    while (!stop_requested_ && ran < limit) {
        const Cycles slice = std::min(kDeviceBatch, limit - ran);
        const Cycles did = step_cpu_rsp(slice);
        if (did == 0) {
            fr.stopped = debugger_->paused();
            break;
        }
        ran += did;
        impl_->flush_devices();

        if (timing_.sync_to_vi && impl_->vi.frame_ready()) {
            fr.vi_frame = true;
            break;
        }
        if (timing_.sync_to_vi && impl_->vi.frame_count() > frames_before) {
            fr.vi_frame = true;
            break;
        }
    }

    if (!impl_->rsp.halted() || impl_->rsp.broke()) {
        impl_->rsp.push_mem_to_bus();
    }

    fr.cycles_ran = ran;
    fr.vi_frame_index = impl_->vi.frame_count();
    fr.hit_limit = !fr.vi_frame && ran >= limit;
    fr.stopped = fr.stopped || stop_requested_ ||
                 (debugger_->enabled() && debugger_->paused());

    // If VI never fired (e.g. weird regs), still report a logical frame so
    // the UI can present whatever is in the FB.
    if (!fr.vi_frame && ran >= budget) {
        fr.vi_frame = true; // budget-based frame
    }
    return fr;
}

bool Emulator::frame_ready() const noexcept {
    return impl_->vi.frame_ready();
}

void Emulator::clear_frame_ready() noexcept {
    impl_->vi.clear_frame_ready();
}

bool Emulator::present_framebuffer(std::vector<u8>& rgba, int& width, int& height) const {
    width = impl_->vi.fb_width();
    height = impl_->vi.fb_height();
    return impl_->vi.copy_framebuffer_rgba(impl_->bus, rgba);
}

u64 Emulator::vi_frame_count() const noexcept {
    return impl_->vi.frame_count();
}

Cycles Emulator::master_cycles() const noexcept {
    return impl_->scheduler.now() + impl_->device_debt;
}

void Emulator::run(Cycles max_cycles) {
    stop_requested_ = false;
    while (!stop_requested_) {
        if (max_cycles != 0 && cycles_executed_ >= max_cycles) {
            break;
        }
        if (max_cycles == 0) {
            const FrameResult frame = run_frame();
            if (frame.stopped || frame.cycles_ran == 0) {
                break;
            }
        } else {
            const Cycles remaining = max_cycles - cycles_executed_;
            // Prefer frame-sized chunks when possible.
            const Cycles chunk = std::min(remaining, timing_.cycles_per_frame
                                                         ? timing_.cycles_per_frame
                                                         : Cycles{1024});
            const Cycles before = cycles_executed_;
            run_cycles(chunk);
            if (cycles_executed_ == before) {
                break;
            }
        }
    }
}

Cpu& Emulator::cpu() noexcept { return impl_->cpu; }
const Cpu& Emulator::cpu() const noexcept { return impl_->cpu; }
Bus& Emulator::bus() noexcept { return impl_->bus; }
const Bus& Emulator::bus() const noexcept { return impl_->bus; }
VideoInterface& Emulator::vi() noexcept { return impl_->vi; }
const VideoInterface& Emulator::vi() const noexcept { return impl_->vi; }
AudioInterface& Emulator::ai() noexcept { return impl_->ai; }
const AudioInterface& Emulator::ai() const noexcept { return impl_->ai; }
Pif& Emulator::pif() noexcept { return impl_->pif; }
const Pif& Emulator::pif() const noexcept { return impl_->pif; }
Rsp& Emulator::rsp() noexcept { return impl_->rsp; }
const Rsp& Emulator::rsp() const noexcept { return impl_->rsp; }
Rdp& Emulator::rdp() noexcept { return impl_->rdp; }
const Rdp& Emulator::rdp() const noexcept { return impl_->rdp; }
Scheduler& Emulator::scheduler() noexcept { return impl_->scheduler; }
const Scheduler& Emulator::scheduler() const noexcept { return impl_->scheduler; }

} // namespace n64
