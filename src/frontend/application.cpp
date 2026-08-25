#include "n64/frontend/application.hpp"

#include "n64/ai/ai.hpp"
#include "n64/cart/header.hpp"
#include "n64/common/log.hpp"
#include "n64/common/version.hpp"
#include "n64/debug/debugger.hpp"

#include <SDL.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace n64::frontend {
using n64::BootMode;
using n64::CicType;
using n64::TvType;
using n64::cic_name;
using n64::tv_name;
namespace {

void print_help(const char* argv0) {
    std::cout
        << project_name() << " " << version().to_string() << "\n"
        << "Usage: " << argv0 << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  --rom PATH          Load a homebrew cartridge image\n"
        << "  --pif-rom PATH      Load user-owned PIF boot ROM (legal dump only)\n"
        << "  --ipl3 PATH         Load user-owned IPL3 blob into SP DMEM\n"
        << "  --lle-pif           Boot in LLE mode at 0xBFC00000 (needs --pif-rom)\n"
        << "  --cic TYPE          Force CIC: 6101|6102|6103|6105|6106|5101|x103\n"
        << "  --tv TYPE           Force TV: ntsc|pal|mpal\n"
        << "  --headless          Force SDL dummy video (no real display)\n"
        << "  --mute              Disable SDL audio output\n"
        << "  --no-vsync-emu      Do not pace to VI/fps (run uncapped)\n"
        << "  --debug             Enable debugger (breakpoints, overlay)\n"
        << "  --trace             Enable CPU instruction trace ring\n"
        << "  --trace-dump PATH   Write trace to file on exit\n"
        << "  --break HEX         Add PC breakpoint (repeatable)\n"
        << "  --auto-close MS     Close the window after MS milliseconds\n"
        << "  --log-level LEVEL   trace|debug|info|warn|error|critical\n"
        << "  --width N           Window width (default 640)\n"
        << "  --height N          Window height (default 480)\n"
        << "  --help              Show this help\n"
        << "\n"
        << "Controls (keyboard):\n"
        << "  Z/X        A / B          Enter     Start\n"
        << "  Q/E        L / R          C         Z-trigger\n"
        << "  Arrows     D-pad          IJKL      C-buttons\n"
        << "  WASD       Analog stick   Esc       Quit\n"
        << "\n"
        << "Debug keys (with --debug):\n"
        << "  F1  toggle overlay     F2  pause/resume\n"
        << "  F3  step one insn      F5  resume\n"
        << "  F6  toggle trace       F9  dump trace to trace.txt\n"
        << "\n"
        << "Legal notice: only use homebrew / self-built ROMs and\n"
        << "user-provided firmware. Do not distribute commercial ROMs or\n"
        << "proprietary PIF/IPL dumps with this project. HLE boot needs no\n"
        << "firmware; --pif-rom/--ipl3 are optional and never shipped here.\n";
}

CicType parse_cic(std::string_view s) {
    if (s == "6101") return CicType::Cic6101;
    if (s == "6102") return CicType::Cic6102;
    if (s == "6103") return CicType::Cic6103;
    if (s == "6105") return CicType::Cic6105;
    if (s == "6106") return CicType::Cic6106;
    if (s == "5101") return CicType::Cic5101;
    if (s == "x103" || s == "X103") return CicType::CicX103;
    if (s == "x105" || s == "X105") return CicType::CicX105;
    if (s == "x106" || s == "X106") return CicType::CicX106;
    std::cerr << "Unknown CIC type: " << s << "\n";
    std::exit(2);
}

TvType parse_tv(std::string_view s) {
    if (s == "ntsc" || s == "NTSC") return TvType::NTSC;
    if (s == "pal"  || s == "PAL")  return TvType::PAL;
    if (s == "mpal" || s == "MPAL") return TvType::MPAL;
    std::cerr << "Unknown TV type: " << s << "\n";
    std::exit(2);
}

spdlog::level::level_enum parse_level(std::string_view s) {
    if (s == "trace") return spdlog::level::trace;
    if (s == "debug") return spdlog::level::debug;
    if (s == "info")  return spdlog::level::info;
    if (s == "warn" || s == "warning") return spdlog::level::warn;
    if (s == "error") return spdlog::level::err;
    if (s == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

} // namespace

AppConfig Application::parse_args(int argc, char** argv) {
    AppConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            std::exit(0);
        } else if (arg == "--rom") {
            cfg.rom_path = need("--rom");
        } else if (arg == "--pif-rom") {
            cfg.boot.pif_rom_path = need("--pif-rom");
        } else if (arg == "--ipl3") {
            cfg.boot.ipl3_path = need("--ipl3");
        } else if (arg == "--lle-pif") {
            cfg.boot.mode = BootMode::LlePif;
        } else if (arg == "--cic") {
            cfg.boot.force_cic = parse_cic(need("--cic"));
        } else if (arg == "--tv") {
            cfg.boot.force_tv_set = true;
            cfg.boot.force_tv = parse_tv(need("--tv"));
        } else if (arg == "--headless") {
            cfg.window.headless = true;
        } else if (arg == "--mute") {
            cfg.mute = true;
        } else if (arg == "--no-vsync-emu") {
            cfg.pace_to_fps = false;
        } else if (arg == "--debug") {
            cfg.debug = true;
        } else if (arg == "--trace") {
            cfg.trace = true;
            cfg.debug = true;
        } else if (arg == "--trace-dump") {
            cfg.trace_dump_path = need("--trace-dump");
            cfg.trace = true;
            cfg.debug = true;
        } else if (arg == "--break") {
            const char* s = need("--break");
            cfg.break_pcs.push_back(std::strtoull(s, nullptr, 0));
            cfg.debug = true;
        } else if (arg == "--auto-close") {
            cfg.auto_close_ms = std::atoi(need("--auto-close"));
        } else if (arg == "--log-level") {
            cfg.log_level = parse_level(need("--log-level"));
        } else if (arg == "--width") {
            cfg.window.width = std::atoi(need("--width"));
        } else if (arg == "--height") {
            cfg.window.height = std::atoi(need("--height"));
        } else if (!arg.empty() && arg[0] != '-') {
            cfg.rom_path = std::string(arg);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_help(argv[0]);
            std::exit(2);
        }
    }
    return cfg;
}

int Application::run(const AppConfig& cfg) {
    n64::log::init("[%H:%M:%S.%e] [%^%l%$] %v", cfg.log_level);

    N64_INFO("{} {} starting", project_name(), version().to_string());

    emulator_.set_boot_config(cfg.boot);

    if (cfg.debug) {
        emulator_.enable_debugger(true);
        if (cfg.trace) {
            emulator_.debugger().set_trace_enabled(true);
            emulator_.debugger().attach_cpu_trace(emulator_.cpu());
        }
        for (u64 pc : cfg.break_pcs) {
            emulator_.debugger().add_breakpoint(pc);
            N64_INFO("Breakpoint @ {:08X}", static_cast<u32>(pc));
        }
    }

    if (!cfg.rom_path.empty()) {
        if (!emulator_.load_rom(cfg.rom_path)) {
            return 1;
        }
        N64_INFO("Booted \"{}\" CIC={} TV={} PC={:08X}",
                 emulator_.cart_header().title(),
                 cic_name(emulator_.cart_header().cic),
                 tv_name(emulator_.cart_header().tv),
                 emulator_.entrypoint());
    } else if (cfg.boot.mode == BootMode::LlePif) {
        // PIF-only boot (no cart) — advanced / research use.
        if (!emulator_.boot(cfg.boot)) {
            return 1;
        }
        N64_INFO("Booted LLE-PIF PC={:08X}", emulator_.entrypoint());
    }

    WindowConfig wcfg = cfg.window;
    wcfg.title = std::string(project_name()) + " " + version().to_string();
    if (emulator_.booted() && !emulator_.cart_header().title().empty()) {
        wcfg.title += " — ";
        wcfg.title += std::string(emulator_.cart_header().title());
    }

    if (!window_.create(wcfg)) {
        N64_ERROR("Failed to create window");
        return 1;
    }

    input_.init();

    if (!cfg.mute) {
        const int rate = static_cast<int>(emulator_.ai().sample_rate());
        if (!audio_.init(&emulator_.ai(), rate > 0 ? rate : 32000)) {
            N64_WARN("Continuing without audio output");
        }
    }

    // Re-attach trace after boot (CPU was reset).
    if (cfg.debug) {
        emulator_.debugger().attach_cpu_trace(emulator_.cpu());
    }

    const auto start = std::chrono::steady_clock::now();
    auto frame_epoch = std::chrono::steady_clock::now();
    bool running = true;
    bool show_overlay = cfg.debug;
    u64 ui_frames = 0;
    u64 emu_frames = 0;
    std::vector<u8> fb_rgba;

    const double target_fps = emulator_.timing().target_fps > 1.0
                                  ? emulator_.timing().target_fps
                                  : 60.0;
    const auto frame_period = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(1.0 / target_fps));

    while (running) {
        const bool alive = window_.poll_events([&](const SDL_Event& e) {
            switch (e.type) {
            case SDL_KEYDOWN:
                if (cfg.debug && e.key.repeat == 0) {
                    auto& dbg = emulator_.debugger();
                    switch (e.key.keysym.sym) {
                    case SDLK_F1:
                        show_overlay = !show_overlay;
                        break;
                    case SDLK_F2:
                        if (dbg.paused()) {
                            dbg.resume();
                        } else {
                            dbg.pause(DebugStopReason::UserPause);
                        }
                        break;
                    case SDLK_F3:
                        dbg.request_step(1);
                        break;
                    case SDLK_F5:
                        dbg.resume();
                        break;
                    case SDLK_F6:
                        dbg.set_trace_enabled(!dbg.trace_enabled());
                        dbg.attach_cpu_trace(emulator_.cpu());
                        N64_INFO("CPU trace {}", dbg.trace_enabled() ? "ON" : "OFF");
                        break;
                    case SDLK_F9:
                        if (emulator_.dump_trace_file("trace.txt", 4096)) {
                            N64_INFO("Wrote trace.txt ({} entries)", dbg.trace_size());
                        }
                        break;
                    default:
                        break;
                    }
                }
                if (e.key.type == SDL_KEYDOWN) {
                    input_.on_key(e.key);
                }
                break;
            case SDL_KEYUP:
                input_.on_key(e.key);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                input_.on_controller_button(e.cbutton);
                break;
            case SDL_CONTROLLERAXISMOTION:
                input_.on_controller_axis(e.caxis);
                break;
            default:
                break;
            }
        });
        if (!alive) {
            running = false;
            break;
        }

        // Push latest pad state into PIF before the CPU runs.
        input_.apply_to(emulator_.pif());

        if (emulator_.booted()) {
            // Paused debugger freezes emulation (F3 request_step unpauses for N insns).
            if (!(cfg.debug && emulator_.debugger().paused())) {
                const auto fr = emulator_.run_frame();
                if (fr.vi_frame) {
                    ++emu_frames;
                }
            }
            audio_.pump();

            int fw = 0, fh = 0;
            if (emulator_.present_framebuffer(fb_rgba, fw, fh) && fw > 0 && fh > 0) {
                window_.clear(0, 0, 0);
                window_.draw_framebuffer(fb_rgba.data(), fw, fh);
            } else {
                window_.clear(16, 24, 48);
            }
        } else {
            const u8 r = 16;
            const u8 g = static_cast<u8>(24 + (ui_frames / 2) % 40);
            const u8 b = static_cast<u8>(48 + (ui_frames) % 80);
            window_.clear(r, g, b);
        }

        if (cfg.debug && show_overlay) {
            window_.draw_overlay_text(emulator_.debugger().format_overlay(emulator_));
            window_.set_title(std::string(project_name()) + " | " +
                              emulator_.debugger().format_status_line(emulator_));
        }

        window_.present();
        ++ui_frames;

        if (cfg.auto_close_ms > 0) {
            const auto elapsed = std::chrono::steady_clock::now() - start;
            const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            if (ms >= cfg.auto_close_ms) {
                N64_INFO("Auto-close after {} ms ({} ui / {} emu frames, {} vi)",
                         ms, ui_frames, emu_frames, emulator_.vi_frame_count());
                running = false;
            }
        }

        // Host frame pacing: high-precision hybrid sleep + spin-wait.
        if (cfg.pace_to_fps) {
            frame_epoch += frame_period;
            auto now = std::chrono::steady_clock::now();
            if (frame_epoch > now) {
                const auto wait_dur = frame_epoch - now;
                if (wait_dur > std::chrono::microseconds(2000)) {
                    std::this_thread::sleep_for(wait_dur - std::chrono::microseconds(1500));
                }
                while (std::chrono::steady_clock::now() < frame_epoch) {
                    std::this_thread::yield();
                }
            } else {
                // Behind schedule — reset epoch if drift is more than 4 frames
                if (now - frame_epoch > frame_period * 4) {
                    frame_epoch = now;
                }
            }
        } else {
            // Uncapped: tiny yield only.
            std::this_thread::yield();
        }
    }

    if (!cfg.trace_dump_path.empty()) {
        if (emulator_.dump_trace_file(cfg.trace_dump_path, 8192)) {
            N64_INFO("Trace dumped to {}", cfg.trace_dump_path);
        } else {
            N64_WARN("Failed to dump trace to {}", cfg.trace_dump_path);
        }
    }

    N64_INFO("Shutting down. cycles={} vi_frames={} emu_frames={} audio_frames={}",
             emulator_.cycles_executed(), emulator_.vi_frame_count(), emu_frames,
             audio_.frames_submitted());
    audio_.shutdown();
    input_.shutdown();
    window_.destroy();
    return 0;
}

} // namespace n64::frontend
