#include <gtest/gtest.h>

#include "n64/bus/bus.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/cpu/insn.hpp"
#include "n64/debug/debugger.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace n64;
using namespace n64::insn;

namespace {

std::vector<u8> make_rom(u32 entry, const std::vector<u32>& code) {
    std::vector<u8> rom(0x1000 + code.size() * 4, 0);
    auto put32 = [&](std::size_t o, u32 v) {
        rom[o] = static_cast<u8>(v >> 24);
        rom[o + 1] = static_cast<u8>(v >> 16);
        rom[o + 2] = static_cast<u8>(v >> 8);
        rom[o + 3] = static_cast<u8>(v);
    };
    put32(0, 0x80371240);
    put32(8, entry);
    for (std::size_t i = 0; i < code.size(); ++i) {
        put32(0x1000 + i * 4, code[i]);
    }
    return rom;
}

} // namespace

TEST(Debugger, BreakpointStopsAtPc) {
    // Code at 0x80000400: nop; nop; addiu t0,0,1; b .
    const u32 entry = 0x80000400u;
    std::vector<u32> code = {
        nop(),
        nop(),
        addiu(8, 0, 1),
        0x1000FFFF, // b .
        nop(),
    };
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(make_rom(entry, code)));
    emu.enable_debugger(true);
    emu.debugger().add_breakpoint(entry + 8); // stop on addiu

    emu.run_cycles(100);
    EXPECT_TRUE(emu.debugger().paused());
    EXPECT_EQ(emu.debugger().stop_reason(), DebugStopReason::Breakpoint);
    EXPECT_EQ(emu.cpu().pc(), entry + 8);
    EXPECT_EQ(emu.cpu().gpr(8), 0u); // addiu not executed yet
    EXPECT_GE(emu.debugger().hit_count(), 1u);
}

TEST(Debugger, StepExecutesOneInstruction) {
    const u32 entry = 0x80000400u;
    std::vector<u32> code = {
        addiu(8, 0, 1),
        addiu(9, 0, 2),
        addiu(10, 0, 3),
        0x1000FFFF,
        nop(),
    };
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(make_rom(entry, code)));
    emu.enable_debugger(true);
    emu.debugger().pause();

    emu.debugger().request_step(1);
    emu.run_cycles(10);
    EXPECT_TRUE(emu.debugger().paused());
    EXPECT_EQ(emu.debugger().stop_reason(), DebugStopReason::StepDone);
    EXPECT_EQ(emu.cpu().pc(), entry + 4);
    EXPECT_EQ(emu.cpu().gpr(8), 1u);
    EXPECT_EQ(emu.cpu().gpr(9), 0u);

    emu.debugger().request_step(2);
    emu.run_cycles(10);
    EXPECT_EQ(emu.cpu().gpr(9), 2u);
    EXPECT_EQ(emu.cpu().gpr(10), 3u);
}

TEST(Debugger, ResumeContinues) {
    const u32 entry = 0x80000400u;
    std::vector<u32> code = {
        addiu(8, 0, 5),
        0x1000FFFF,
        nop(),
    };
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(make_rom(entry, code)));
    emu.enable_debugger(true);
    emu.debugger().add_breakpoint(entry);
    emu.run_cycles(5);
    EXPECT_TRUE(emu.debugger().paused());

    emu.debugger().remove_breakpoint(entry);
    emu.debugger().resume();
    emu.run_cycles(5);
    EXPECT_FALSE(emu.debugger().paused());
    EXPECT_EQ(emu.cpu().gpr(8), 5u);
}

TEST(Debugger, ResumeExecutesInstructionAtCurrentBreakpointOnce) {
    const u32 entry = 0x80000400u;
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(make_rom(entry, {
        addiu(8, 0, 5),
        0x1000FFFF,
        nop(),
    })));
    emu.enable_debugger(true);
    ASSERT_TRUE(emu.debugger().add_breakpoint(entry));
    emu.run_cycles(5);
    ASSERT_TRUE(emu.debugger().paused());

    emu.debugger().resume();
    emu.run_cycles(5);
    EXPECT_EQ(emu.cpu().gpr(8), 5u);
    EXPECT_FALSE(emu.debugger().paused());
    EXPECT_TRUE(emu.debugger().has_breakpoint(entry));
}

TEST(Debugger, StepExecutesInstructionAtCurrentBreakpoint) {
    const u32 entry = 0x80000400u;
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(make_rom(entry, {
        addiu(8, 0, 9),
        0x1000FFFF,
        nop(),
    })));
    emu.enable_debugger(true);
    ASSERT_TRUE(emu.debugger().add_breakpoint(entry));
    emu.run_cycles(5);
    ASSERT_TRUE(emu.debugger().paused());

    emu.debugger().request_step(1);
    emu.run_cycles(5);
    EXPECT_EQ(emu.cpu().gpr(8), 9u);
    EXPECT_TRUE(emu.debugger().paused());
    EXPECT_EQ(emu.debugger().stop_reason(), DebugStopReason::StepDone);
    EXPECT_EQ(emu.cpu().pc(), entry + 4);
}

TEST(Debugger, RunReturnsAndFrameReportsDebuggerPause) {
    const u32 entry = 0x80000400u;
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(make_rom(entry, {nop(), 0x1000FFFF, nop()})));
    emu.enable_debugger(true);
    ASSERT_TRUE(emu.debugger().add_breakpoint(entry));

    const FrameResult frame = emu.run_frame();
    EXPECT_TRUE(frame.stopped);
    EXPECT_EQ(frame.cycles_ran, 0u);
    EXPECT_TRUE(emu.debugger().paused());

    const Cycles before = emu.cycles_executed();
    emu.run(before + 100);
    EXPECT_EQ(emu.cycles_executed(), before);
}

TEST(Debugger, TraceRingRecords) {
    const u32 entry = 0x80000400u;
    std::vector<u32> code = {
        addiu(4, 0, 1),
        addiu(5, 0, 2),
        addu(6, 4, 5),
        0x1000FFFF,
        nop(),
    };
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(make_rom(entry, code)));
    emu.enable_debugger(true);
    emu.debugger().set_trace_enabled(true);
    emu.debugger().attach_cpu_trace(emu.cpu());
    emu.debugger().set_trace_capacity(64);

    emu.run_cycles(10);
    EXPECT_GE(emu.debugger().trace_size(), 3u);
    const auto lines = emu.debugger().recent_trace(10);
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines.front().pc, entry);
    ASSERT_GE(lines.size(), 2u);
    EXPECT_LT(lines[0].cycle, lines[1].cycle);

    const std::string formatted = emu.debugger().format_trace(5);
    EXPECT_FALSE(formatted.empty());
    // PC appears as 8 hex digits (case-insensitive).
    bool found_pc = false;
    for (const auto& e : lines) {
        if (e.pc == entry) {
            found_pc = true;
            break;
        }
    }
    EXPECT_TRUE(found_pc);
}

TEST(Debugger, TraceDumpFile) {
    const u32 entry = 0x80000400u;
    std::vector<u32> code = {addiu(2, 0, 7), 0x1000FFFF, nop()};
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(make_rom(entry, code)));
    emu.enable_debugger(true);
    emu.debugger().set_trace_enabled(true);
    emu.debugger().attach_cpu_trace(emu.cpu());
    emu.run_cycles(5);

    const std::string path = (std::filesystem::temp_directory_path() / "n64emu_test_trace.txt").string();
    ASSERT_TRUE(emu.dump_trace_file(path, 32));
    std::ifstream in(path);
    ASSERT_TRUE(in.good());
    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("80000400"), std::string::npos);
}

TEST(Debugger, WatchpointWrite) {
    const u32 entry = 0x80000400u;
    // sw t0, 0(t1) with t1=0x80001000 → phys 0x1000
    std::vector<u32> code = {
        addiu(8, 0, 0x1234),
        lui(9, 0x8000),
        ori(9, 9, 0x1000),
        sw(8, 9, 0),
        0x1000FFFF,
        nop(),
    };
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(make_rom(entry, code)));
    emu.enable_debugger(true);
    const int id = emu.debugger().add_watchpoint(0x1000, false, true);
    EXPECT_GE(id, 0);

    emu.run_cycles(20);
    EXPECT_TRUE(emu.debugger().paused());
    EXPECT_EQ(emu.debugger().stop_reason(), DebugStopReason::Watchpoint);
    EXPECT_GE(emu.debugger().watch_hit_count(), 1u);
}

TEST(Debugger, WatchpointIdsRemainStableAfterRemoval) {
    Debugger debugger;
    const int first = debugger.add_watchpoint(0x1000, false, true);
    const int second = debugger.add_watchpoint(0x2000, false, true);
    const int third = debugger.add_watchpoint(0x3000, false, true);
    ASSERT_GT(first, 0);
    ASSERT_GT(second, first);
    ASSERT_GT(third, second);
    EXPECT_TRUE(debugger.remove_watchpoint(first));
    EXPECT_TRUE(debugger.remove_watchpoint(third));
    EXPECT_FALSE(debugger.remove_watchpoint(first));
    ASSERT_EQ(debugger.watchpoints().size(), 1u);
    EXPECT_EQ(debugger.watchpoints().front().id, second);
}

TEST(Debugger, WatchpointDetectsOverlappingWideAccess) {
    Emulator emu;
    emu.enable_debugger(true);
    ASSERT_GT(emu.debugger().add_watchpoint(0x1002, false, true), 0);
    emu.bus().write32(0x1000, 0xDEAD'BEEFu);
    EXPECT_TRUE(emu.debugger().paused());
    EXPECT_EQ(emu.debugger().stop_reason(), DebugStopReason::Watchpoint);
}

TEST(Debugger, UnknownMmioLogged) {
    Emulator emu;
    emu.enable_debugger(true);
    // Write to an unmapped MMIO-ish address in the RCP hole
    emu.bus().write32(0x0490'0000u, 0xDEADBEEF);
    EXPECT_GE(emu.debugger().unknown_mmio_count(), 1u);
    const std::string s = emu.debugger().format_unknown_mmio(4);
    EXPECT_NE(s.find("04900000"), std::string::npos);
}

TEST(Debugger, OverlayNotEmpty) {
    const u32 entry = 0x80000400u;
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(make_rom(entry, {0x1000FFFF, 0})));
    emu.enable_debugger(true);
    const std::string ov = emu.debugger().format_overlay(emu);
    EXPECT_NE(ov.find("PC="), std::string::npos);
    EXPECT_NE(ov.find("DEBUG"), std::string::npos);
    EXPECT_FALSE(emu.debugger().format_status_line(emu).empty());
}

TEST(Debugger, DisabledDoesNotStop) {
    const u32 entry = 0x80000400u;
    std::vector<u32> code = {addiu(8, 0, 1), 0x1000FFFF, nop()};
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(make_rom(entry, code)));
    // Debugger off — breakpoint ignored
    emu.debugger().add_breakpoint(entry);
    emu.run_cycles(5);
    EXPECT_FALSE(emu.debugger().paused());
    EXPECT_EQ(emu.cpu().gpr(8), 1u);
}
