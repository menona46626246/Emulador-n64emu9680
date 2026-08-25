#include <gtest/gtest.h>

#include "n64/bus/bus.hpp"
#include "n64/common/types.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/block_cache.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/cpu/insn.hpp"

#include <chrono>
#include <vector>

using namespace n64;
using namespace n64::insn;

TEST(BlockCache, CompilationAndLookup) {
    Bus bus(kRdramSize);
    Cpu cpu;
    cpu.connect_bus(&bus);

    // Put linear instructions at 0x80000000 (RDRAM paddr 0x00000000)
    // lui t0, 0x8000
    // ori t0, t0, 0x1000
    // addiu t1, t0, 10
    // sw t1, 0(t0)
    // beq t1, t1, +1  (branch target + delay slot)
    // nop (delay slot)
    const u64 pc = 0x80000000ull;
    bus.write32(0x0000, lui(8, 0x8000));
    bus.write32(0x0004, ori(8, 8, 0x1000));
    bus.write32(0x0008, addiu(9, 8, 10));
    bus.write32(0x000C, sw(9, 8, 0));
    bus.write32(0x0010, beq(9, 9, 1));
    bus.write32(0x0014, nop());

    BlockCache cache;
    EXPECT_EQ(cache.lookup(pc), nullptr);
    EXPECT_EQ(cache.misses(), 1u);

    const BasicBlock* blk = cache.compile(pc, bus, cpu);
    ASSERT_NE(blk, nullptr);
    EXPECT_EQ(blk->start_pc, pc);
    EXPECT_EQ(blk->paddr, 0x0000u);
    EXPECT_EQ(blk->insns.size(), 6u);
    EXPECT_TRUE(blk->ends_with_branch);

    // Verify lookup now hits
    const BasicBlock* blk2 = cache.lookup(pc);
    ASSERT_NE(blk2, nullptr);
    EXPECT_EQ(blk2, blk);
    EXPECT_EQ(cache.hits(), 1u);
}

TEST(BlockCache, ExecutionParity) {
    // Setup a loop that computes sum 1..100 in two separate CPUs:
    // CPU 1: standard step-by-step
    // CPU 2: accelerated run() with BlockCache
    const u32 entry = 0x80000400u;
    std::vector<u32> payload = {
        lui(8, 0x8000),
        ori(8, 8, 0x1000),
        addiu(9, 0, 0),       // sum = 0
        addiu(10, 0, 1),      // i = 1
        addiu(11, 0, 101),    // limit = 101
        // loop at offset 5*4 = 0x14:
        addu(9, 9, 10),       // sum += i
        sw(9, 8, 0),          // *0x80001000 = sum
        addiu(10, 10, 1),     // ++i
        bne(10, 11, -4),      // loop if i != 101
        nop(),                // delay slot
        beq(0, 0, -1),        // halt loop
        nop(),
    };

    std::vector<u8> rom(0x1000 + payload.size() * 4, 0);
    auto put32 = [&](std::size_t o, u32 v) {
        rom[o] = static_cast<u8>(v >> 24);
        rom[o + 1] = static_cast<u8>(v >> 16);
        rom[o + 2] = static_cast<u8>(v >> 8);
        rom[o + 3] = static_cast<u8>(v);
    };
    put32(0, 0x80371240);
    put32(8, entry);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        put32(0x1000 + i * 4, payload[i]);
    }

    // Emulator with BlockCache enabled
    Emulator emu_cached;
    ASSERT_TRUE(emu_cached.load_rom_bytes(rom));
    emu_cached.cpu().set_block_cache_enabled(true);
    emu_cached.run_cycles(10000);
    ASSERT_NE(emu_cached.cpu().block_cache(), nullptr);
    EXPECT_GT(emu_cached.cpu().block_cache()->hits(), 0u);

    // Sum of 1..100 = 5050
    EXPECT_EQ(emu_cached.cpu().gpr(9), 5050u);
    EXPECT_EQ(emu_cached.bus().read32(0x1000), 5050u);

    // Emulator with BlockCache disabled
    Emulator emu_uncached;
    ASSERT_TRUE(emu_uncached.load_rom_bytes(rom));
    emu_uncached.cpu().set_block_cache_enabled(false);
    emu_uncached.run_cycles(10000);

    EXPECT_EQ(emu_uncached.cpu().gpr(9), 5050u);
    EXPECT_EQ(emu_uncached.bus().read32(0x1000), 5050u);
}

TEST(BlockCache, InvalidationOnMemoryWrite) {
    Bus bus(kRdramSize);
    Cpu cpu;
    cpu.connect_bus(&bus);
    BlockCache cache;

    const u64 pc = 0x80000000ull;
    bus.write32(0x0000, nop());
    bus.write32(0x0004, nop());

    ASSERT_NE(cache.compile(pc, bus, cpu), nullptr);
    EXPECT_NE(cache.lookup(pc), nullptr);

    // Invalidate paddr 0x0000
    cache.invalidate(0x0000, 4);
    EXPECT_EQ(cache.lookup(pc), nullptr);

    // Re-compile and clear all
    ASSERT_NE(cache.compile(pc, bus, cpu), nullptr);
    EXPECT_NE(cache.lookup(pc), nullptr);
    cache.clear();
    EXPECT_EQ(cache.lookup(pc), nullptr);
}

TEST(BlockCache, EmulatorBusWriteInvalidatesCompiledCode) {
    const u32 entry = 0x80000400u;
    std::vector<u8> rom(0x1010, 0);
    const auto put32 = [&rom](std::size_t offset, u32 value) {
        rom[offset] = static_cast<u8>(value >> 24);
        rom[offset + 1] = static_cast<u8>(value >> 16);
        rom[offset + 2] = static_cast<u8>(value >> 8);
        rom[offset + 3] = static_cast<u8>(value);
    };
    put32(0, 0x80371240);
    put32(8, entry);
    put32(0x1000, addiu(8, 0, 1));
    put32(0x1004, beq(0, 0, -2));
    put32(0x1008, nop());

    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    emu.cpu().set_block_cache_enabled(true);
    emu.run_cycles(64);
    BlockCache* cache = emu.cpu().block_cache();
    ASSERT_NE(cache, nullptr);
    ASSERT_GT(cache->size(), 0u);

    emu.bus().write32(0x400, addiu(8, 0, 2));
    EXPECT_EQ(cache->lookup(entry), nullptr);
    emu.cpu().set_gpr(8, 0);
    emu.cpu().set_pc(entry);
    emu.run_cycles(1);
    EXPECT_EQ(emu.cpu().gpr(8), 2u);
}

TEST(BlockCache, BranchLikelyNotTakenNullifiesCachedDelaySlot) {
    const u32 entry = 0x80000400u;
    std::vector<u8> rom(0x1020, 0);
    const auto put32 = [&rom](std::size_t offset, u32 value) {
        rom[offset] = static_cast<u8>(value >> 24);
        rom[offset + 1] = static_cast<u8>(value >> 16);
        rom[offset + 2] = static_cast<u8>(value >> 8);
        rom[offset + 3] = static_cast<u8>(value);
    };
    put32(0, 0x80371240);
    put32(8, entry);
    put32(0x1000, beql(4, 5, 1));
    put32(0x1004, addiu(8, 0, 99));
    put32(0x1008, addiu(9, 0, 7));
    put32(0x100C, beq(0, 0, -1));
    put32(0x1010, nop());

    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    emu.cpu().set_gpr(4, 1);
    emu.cpu().set_gpr(5, 2);
    emu.cpu().set_block_cache_enabled(true);
    emu.run_cycles(64);
    EXPECT_EQ(emu.cpu().gpr(8), 0u);
    EXPECT_EQ(emu.cpu().gpr(9), 7u);
    ASSERT_NE(emu.cpu().block_cache(), nullptr);
    EXPECT_GT(emu.cpu().block_cache()->hits(), 0u);
}
