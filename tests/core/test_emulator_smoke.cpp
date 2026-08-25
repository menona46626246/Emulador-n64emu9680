#include <gtest/gtest.h>

#include "n64/ai/ai.hpp"
#include "n64/bus/bus.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/pif/pif.hpp"
#include "n64/rcp/rdp/rdp.hpp"
#include "n64/rcp/rsp/rsp.hpp"
#include "n64/vi/vi.hpp"

using namespace n64;

TEST(EmulatorSmoke, ConstructAndReset) {
    Emulator emu;
    EXPECT_FALSE(emu.rom_loaded());
    EXPECT_EQ(emu.cycles_executed(), 0u);

    emu.reset();
    EXPECT_EQ(emu.cpu().pc(), 0xBFC0'0000ull);
    EXPECT_EQ(emu.cpu().gpr(0), 0u);
    EXPECT_FALSE(emu.cpu().halted());
}

TEST(EmulatorSmoke, RunCyclesAdvances) {
    Emulator emu;
    // Point CPU at RDRAM KSEG0 filled with NOPs so fetch succeeds.
    emu.bus().write32(0x0000'0000, 0);
    emu.bus().write32(0x0000'0004, 0);
    emu.bus().write32(0x0000'0008, 0);
    // Fill a small run of NOPs
    for (u32 i = 0; i < 128; ++i) {
        emu.bus().write32(i * 4, 0);
    }
    emu.cpu().set_pc(0x8000'0000ull);
    emu.run_cycles(100);
    EXPECT_EQ(emu.cycles_executed(), 100u);
    EXPECT_EQ(emu.cpu().cycles(), 100u);
    EXPECT_EQ(emu.cpu().pc(), 0x8000'0000ull + 100ull * 4ull);
    EXPECT_EQ(emu.cpu().exception_count(), 0u);
}

TEST(EmulatorSmoke, StopRequest) {
    Emulator emu;
    emu.request_stop();
    emu.run_cycles(50);
    EXPECT_EQ(emu.cycles_executed(), 0u);
}

TEST(EmulatorSmoke, ComponentsAccessible) {
    Emulator emu;
    EXPECT_EQ(emu.bus().rdram_size(), kRdramSize);
    EXPECT_TRUE(emu.rsp().halted());
    EXPECT_FALSE(emu.rdp().busy());
    EXPECT_EQ(emu.vi().fb_width(), 320);
    EXPECT_TRUE(emu.pif().controller(0).present);
}
