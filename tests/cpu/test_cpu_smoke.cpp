#include <gtest/gtest.h>

#include "n64/bus/bus.hpp"
#include "n64/cpu/cpu.hpp"

using namespace n64;

TEST(CpuSmoke, ResetVector) {
    Cpu cpu;
    cpu.reset();
    EXPECT_EQ(cpu.pc(), 0xBFC0'0000ull);
    EXPECT_EQ(cpu.gpr(0), 0u);
    EXPECT_EQ(cpu.cycles(), 0u);
}

TEST(CpuSmoke, ZeroRegisterHardwired) {
    Cpu cpu;
    cpu.reset();
    cpu.set_gpr(0, 0xFFFF'FFFFull);
    EXPECT_EQ(cpu.gpr(0), 0u);

    cpu.set_gpr(2, 0x1234);
    EXPECT_EQ(cpu.gpr(2), 0x1234u);
}

TEST(CpuSmoke, StepAdvancesPc) {
    // Without a bus, fetch will error-halt; connect a bus with NOPs in PIF region.
    // Use KSEG0 RDRAM instead via harness-less setup:
    Bus bus;
    bus.reset();
    // Place NOPs at physical 0 so KSEG0 0x80000000 fetches work if we set PC there.
    // Default reset PC is 0xBFC00000 (PIF) — open bus. Point PC at RDRAM KSEG0.
    Cpu cpu;
    cpu.reset();
    cpu.connect_bus(&bus);
    cpu.set_pc(0x8000'0000ull);
    bus.write32(0x0000'0000, 0); // NOP
    bus.write32(0x0000'0004, 0);
    bus.write32(0x0000'0008, 0);
    const u64 start = cpu.pc();
    cpu.step();
    EXPECT_EQ(cpu.pc(), start + 4);
    EXPECT_EQ(cpu.cycles(), 1u);
    cpu.step();
    EXPECT_EQ(cpu.pc(), start + 8);
    EXPECT_EQ(cpu.cycles(), 2u);
}

TEST(CpuSmoke, HaltedStillCountsCycles) {
    Cpu cpu;
    cpu.reset();
    const u64 pc_before = cpu.pc();
    cpu.set_halted(true);
    cpu.step();
    EXPECT_EQ(cpu.pc(), pc_before);
    EXPECT_EQ(cpu.cycles(), 1u);
}
