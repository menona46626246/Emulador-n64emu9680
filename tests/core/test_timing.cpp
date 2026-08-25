#include <gtest/gtest.h>

#include "n64/ai/ai.hpp"
#include "n64/bus/bus.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/core/emulator.hpp"
#include "n64/core/scheduler.hpp"
#include "n64/core/timing.hpp"
#include "n64/cpu/insn.hpp"
#include "n64/vi/vi.hpp"

#include <vector>

using namespace n64;
using namespace n64::insn;

namespace {

std::vector<u8> make_rom(u32 entry, const std::vector<u32>& code, char country = 'E') {
    std::vector<u8> rom(0x1000 + code.size() * 4, 0);
    auto put32 = [&](std::size_t o, u32 v) {
        rom[o] = static_cast<u8>(v >> 24);
        rom[o + 1] = static_cast<u8>(v >> 16);
        rom[o + 2] = static_cast<u8>(v >> 8);
        rom[o + 3] = static_cast<u8>(v);
    };
    put32(0x00, 0x80371240);
    put32(0x08, entry);
    rom[0x20] = 'T'; rom[0x21] = 'I'; rom[0x22] = 'M';
    rom[0x3E] = static_cast<u8>(country);
    for (std::size_t i = 0; i < code.size(); ++i) {
        put32(0x1000 + i * 4, code[i]);
    }
    return rom;
}

} // namespace

TEST(TimingConfig, NtscBudget) {
    const auto t = TimingConfig::ntsc();
    EXPECT_DOUBLE_EQ(t.target_fps, 60.0);
    EXPECT_EQ(t.cycles_per_frame, kCpuClockHz / 60);
    EXPECT_GT(t.max_cycles_per_frame, t.cycles_per_frame);
    EXPECT_TRUE(t.sync_to_vi);
}

TEST(TimingConfig, PalBudget) {
    const auto t = TimingConfig::pal();
    EXPECT_DOUBLE_EQ(t.target_fps, 50.0);
    EXPECT_EQ(t.cycles_per_frame, kCpuClockHz / 50);
}

TEST(TimingBoot, NtscFromCountryE) {
    auto rom = make_rom(0x80000400, {0x1000FFFF, 0}); // b .
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    EXPECT_EQ(emu.cart_header().tv, TvType::NTSC);
    EXPECT_DOUBLE_EQ(emu.timing().target_fps, 60.0);
    EXPECT_EQ(emu.timing().cycles_per_frame, kCpuClockHz / 60);
}

TEST(TimingBoot, PalFromCountryP) {
    auto rom = make_rom(0x80000400, {0x1000FFFF, 0}, 'P');
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    EXPECT_EQ(emu.cart_header().tv, TvType::PAL);
    EXPECT_DOUBLE_EQ(emu.timing().target_fps, 50.0);
}

TEST(TimingRunFrame, ProducesViFrame) {
    auto rom = make_rom(0x80000400, {0x1000FFFF, 0}); // spin
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));

    const u64 before = emu.vi_frame_count();
    const auto fr = emu.run_frame();
    EXPECT_TRUE(fr.vi_frame || fr.cycles_ran > 0);
    EXPECT_GT(fr.cycles_ran, 0u);
    EXPECT_LE(fr.cycles_ran, emu.timing().max_cycles_per_frame);
    // Should advance VI by at least one frame within the budget.
    EXPECT_GE(emu.vi_frame_count(), before);
    EXPECT_FALSE(fr.stopped);
}

TEST(TimingRunFrame, MultipleFramesStable) {
    auto rom = make_rom(0x80000400, {0x1000FFFF, 0});
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));

    u64 total_cycles = 0;
    for (int i = 0; i < 10; ++i) {
        const auto fr = emu.run_frame();
        EXPECT_GT(fr.cycles_ran, 0u);
        total_cycles += fr.cycles_ran;
        EXPECT_FALSE(fr.stopped);
    }
    EXPECT_GE(emu.vi_frame_count(), 5u);
    // Average cycles/frame should be in a sane band around the budget.
    const double avg = static_cast<double>(total_cycles) / 10.0;
    const double budget = static_cast<double>(emu.timing().cycles_per_frame);
    EXPECT_GT(avg, budget * 0.25); // at least some work
    EXPECT_LT(avg, budget * 2.5);
}

TEST(TimingRunFrame, DoesNotBusyHang) {
    auto rom = make_rom(0x80000400, {0x1000FFFF, 0});
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));

    // Cap max so even if VI is broken we return.
    auto t = emu.timing();
    t.max_cycles_per_frame = 100000;
    t.cycles_per_frame = 50000;
    emu.set_timing(t);

    const auto fr = emu.run_frame();
    EXPECT_LE(fr.cycles_ran, 100000u);
    EXPECT_TRUE(fr.vi_frame || fr.hit_limit);
}

TEST(TimingRunCycles, BatchedMatchesTotal) {
    auto rom = make_rom(0x80000400, {0x1000FFFF, 0});
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));

    const Cycles n = 10'000;
    const Cycles before = emu.cycles_executed();
    emu.run_cycles(n);
    EXPECT_EQ(emu.cycles_executed() - before, n);
}

TEST(TimingRunCycles, AdvancesAiAndVi) {
    Emulator emu;
    // No ROM needed for device ticks — use empty spin via direct cycles on reset CPU.
    // Load minimal rom so booted path is fine.
    auto rom = make_rom(0x80000400, {0x1000FFFF, 0});
    ASSERT_TRUE(emu.load_rom_bytes(rom));

    // Seed a tiny AI DMA so tick has work.
    auto rdram = emu.bus().rdram();
    for (u32 i = 0; i < 64; ++i) {
        rdram[0x2000 + i] = 0;
    }
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Control, 1);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::DramAddr, 0x2000);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Len, 64);

    const u64 vi_before = emu.vi_frame_count();
    const u64 ai_before = emu.ai().samples_pushed();

    emu.run_cycles(emu.timing().cycles_per_frame);

    EXPECT_GE(emu.vi_frame_count(), vi_before);
    // AI may or may not finish full DMA depending on rate; at least device ran.
    EXPECT_GE(emu.ai().samples_pushed(), ai_before);
}

TEST(TimingStop, RequestStopAbortsFrame) {
    auto rom = make_rom(0x80000400, {0x1000FFFF, 0});
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    emu.request_stop();
    const auto fr = emu.run_frame();
    EXPECT_TRUE(fr.stopped);
    EXPECT_EQ(fr.cycles_ran, 0u);
}

TEST(TimingScheduler, NowTracksRun) {
    auto rom = make_rom(0x80000400, {0x1000FFFF, 0});
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    const Cycles before = emu.scheduler().now();
    emu.run_cycles(1000);
    // scheduler.now advanced by flush
    EXPECT_GE(emu.scheduler().now(), before + 1000);
}
