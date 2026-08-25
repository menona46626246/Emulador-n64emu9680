#include <gtest/gtest.h>

#include "n64/ai/ai.hpp"
#include "n64/bus/bus.hpp"
#include "n64/common/types.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/cpu/insn.hpp"
#include "n64/vi/vi.hpp"

#include <chrono>
#include <thread>
#include <vector>

using namespace n64;
using namespace n64::insn;

// =============================================================================
// 1. Bus fast-path & native byte swap parity tests
// =============================================================================

TEST(BusFastPath, RdramReadWriteParity) {
    Bus bus(kRdramSize);

    // Test 16-bit, 32-bit and 64-bit writes and verify memory layout
    bus.write16(0x00000000, 0x1234);
    bus.write32(0x00000004, 0xA1B2C3D4);
    bus.write64(0x00000008, 0x0123456789ABCDEFull);

    // Check byte-by-byte big-endian representation
    EXPECT_EQ(bus.read8(0x00000000), 0x12);
    EXPECT_EQ(bus.read8(0x00000001), 0x34);

    EXPECT_EQ(bus.read8(0x00000004), 0xA1);
    EXPECT_EQ(bus.read8(0x00000005), 0xB2);
    EXPECT_EQ(bus.read8(0x00000006), 0xC3);
    EXPECT_EQ(bus.read8(0x00000007), 0xD4);

    EXPECT_EQ(bus.read8(0x00000008), 0x01);
    EXPECT_EQ(bus.read8(0x00000009), 0x23);
    EXPECT_EQ(bus.read8(0x0000000A), 0x45);
    EXPECT_EQ(bus.read8(0x0000000B), 0x67);
    EXPECT_EQ(bus.read8(0x0000000C), 0x89);
    EXPECT_EQ(bus.read8(0x0000000D), 0xAB);
    EXPECT_EQ(bus.read8(0x0000000E), 0xCD);
    EXPECT_EQ(bus.read8(0x0000000F), 0xEF);

    // Fast-path read parity
    EXPECT_EQ(bus.read16(0x00000000), 0x1234);
    EXPECT_EQ(bus.read32(0x00000004), 0xA1B2C3D4u);
    EXPECT_EQ(bus.read64(0x00000008), 0x0123456789ABCDEFull);
}

TEST(BusFastPath, SpMemoryAndCartridgeFastPath) {
    Bus bus(kRdramSize);

    // Test SP DMEM
    bus.write32(0x04000000, 0xCAFEBABE);
    EXPECT_EQ(bus.read32(0x04000000), 0xCAFEBABEu);
    EXPECT_EQ(bus.sp_dmem()[0], 0xCA);
    EXPECT_EQ(bus.sp_dmem()[1], 0xFE);
    EXPECT_EQ(bus.sp_dmem()[2], 0xBA);
    EXPECT_EQ(bus.sp_dmem()[3], 0xBE);

    // Test SP IMEM
    bus.write32(0x04001000, 0xDEADBEEF);
    EXPECT_EQ(bus.read32(0x04001000), 0xDEADBEEFu);
    EXPECT_EQ(bus.sp_imem()[0], 0xDE);
    EXPECT_EQ(bus.sp_imem()[1], 0xAD);
    EXPECT_EQ(bus.sp_imem()[2], 0xBE);
    EXPECT_EQ(bus.sp_imem()[3], 0xEF);
}

// =============================================================================
// 2. VI LUT & Framebuffer Conversion Tests
// =============================================================================

TEST(ViLut, Framebuffer16BitParity) {
    Bus bus(kRdramSize);
    VideoInterface vi;

    // Set 16-bit mode (width=320, height=240)
    vi.write(VideoInterface::Control, 0x00003202u);
    vi.write(VideoInterface::Origin, 0x00001000u);
    vi.write(VideoInterface::Width, 320);
    vi.write(VideoInterface::VStart, (0x0025u << 16) | (0x0025u + 480));

    // Fill first line with known RGBA5551 test pixels:
    // Pure Red:   11111 00000 00000 1 -> 0xF801
    // Pure Green: 00000 11111 00000 1 -> 0x07C1
    // Pure Blue:  00000 00000 11111 1 -> 0x003F
    // White:      11111 11111 11111 1 -> 0xFFFF
    // Black:      00000 00000 00000 1 -> 0x0001
    bus.write16(0x1000, 0xF801);
    bus.write16(0x1002, 0x07C1);
    bus.write16(0x1004, 0x003F);
    bus.write16(0x1006, 0xFFFF);
    bus.write16(0x1008, 0x0001);

    std::vector<u8> rgba;
    ASSERT_TRUE(vi.copy_framebuffer_rgba(bus, rgba));
    ASSERT_GE(rgba.size(), 5 * 4u);

    // Pixel 0: Red
    EXPECT_EQ(rgba[0], 0xFF); // R
    EXPECT_EQ(rgba[1], 0x00); // G
    EXPECT_EQ(rgba[2], 0x00); // B
    EXPECT_EQ(rgba[3], 0xFF); // A

    // Pixel 1: Green
    EXPECT_EQ(rgba[4], 0x00);
    EXPECT_EQ(rgba[5], 0xFF);
    EXPECT_EQ(rgba[6], 0x00);
    EXPECT_EQ(rgba[7], 0xFF);

    // Pixel 2: Blue
    EXPECT_EQ(rgba[8],  0x00);
    EXPECT_EQ(rgba[9],  0x00);
    EXPECT_EQ(rgba[10], 0xFF);
    EXPECT_EQ(rgba[11], 0xFF);

    // Pixel 3: White
    EXPECT_EQ(rgba[12], 0xFF);
    EXPECT_EQ(rgba[13], 0xFF);
    EXPECT_EQ(rgba[14], 0xFF);
    EXPECT_EQ(rgba[15], 0xFF);

    // Pixel 4: Black (opaque)
    EXPECT_EQ(rgba[16], 0x00);
    EXPECT_EQ(rgba[17], 0x00);
    EXPECT_EQ(rgba[18], 0x00);
    EXPECT_EQ(rgba[19], 0xFF);
}

// =============================================================================
// 3. Audio SPSC lock-free ring buffer tests
// =============================================================================

TEST(AiSpsc, BasicPushAndPull) {
    AudioInterface ai;
    ai.reset();

    EXPECT_EQ(ai.buffered_frames(), 0u);

    // Push 4 frames
    ai.push_frame(100, -100);
    ai.push_frame(200, -200);
    ai.push_frame(300, -300);
    ai.push_frame(400, -400);

    EXPECT_EQ(ai.buffered_frames(), 4u);
    EXPECT_EQ(ai.samples_pushed(), 4u);

    std::vector<s16> out(8);
    const std::size_t got = ai.pull_frames(out);
    EXPECT_EQ(got, 4u);
    EXPECT_EQ(ai.buffered_frames(), 0u);

    EXPECT_EQ(out[0], 100);
    EXPECT_EQ(out[1], -100);
    EXPECT_EQ(out[2], 200);
    EXPECT_EQ(out[3], -200);
    EXPECT_EQ(out[4], 300);
    EXPECT_EQ(out[5], -300);
    EXPECT_EQ(out[6], 400);
    EXPECT_EQ(out[7], -400);
}

TEST(AiSpsc, ConcurrentProducerConsumer) {
    AudioInterface ai;
    ai.reset();

    constexpr std::size_t kTotalFrames = 50000;
    std::atomic<bool> producer_done{false};

    // Producer thread
    std::thread producer([&]() {
        for (std::size_t i = 0; i < kTotalFrames; ++i) {
            const s16 sample = static_cast<s16>(i & 0x7FFF);
            ai.push_frame(sample, static_cast<s16>(-sample));
            if ((i & 0xFF) == 0) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Consumer thread
    std::size_t frames_received = 0;
    std::vector<s16> chunk(256 * 2);
    while (!producer_done.load(std::memory_order_acquire) || ai.buffered_frames() > 0) {
        const std::size_t pulled = ai.pull_frames(chunk);
        frames_received += pulled;
        if (pulled == 0) {
            std::this_thread::yield();
        }
    }

    producer.join();

    EXPECT_GT(frames_received, 0u);
    EXPECT_EQ(ai.samples_pushed(), kTotalFrames);
}

TEST(AiSpsc, OverrunDropsNewestWithoutOverwritingUnreadFrames) {
    AudioInterface ai;
    ai.reset();

    // Push more than capacity (8192 frames)
    constexpr std::size_t kPushCount = 9000;
    for (std::size_t i = 0; i < kPushCount; ++i) {
        ai.push_frame(static_cast<s16>(i), static_cast<s16>(i));
    }

    // Capacity is 8192. The producer owns only the write index, so it rejects
    // newest frames instead of racing the consumer to discard old ones.
    EXPECT_EQ(ai.buffered_frames(), AudioInterface::kRingFrames);
    EXPECT_EQ(ai.samples_pushed(), kPushCount);
    EXPECT_EQ(ai.frames_dropped(), kPushCount - AudioInterface::kRingFrames);

    std::vector<s16> out(16);
    const std::size_t got = ai.pull_frames(out);
    ASSERT_EQ(got, 8u);
    for (std::size_t i = 0; i < got; ++i) {
        EXPECT_EQ(out[i * 2], static_cast<s16>(i));
        EXPECT_EQ(out[i * 2 + 1], static_cast<s16>(i));
    }
}

// =============================================================================
// 4. Performance & Core Execution Benchmark
// =============================================================================

TEST(Optimization, HighThroughputExecution) {
    // Rom with a tight loop computing a sum in RDRAM
    // lui t0, 0x8000; ori t0, t0, 0x1000;
    // loop: addiu t1, t1, 1; sw t1, 0(t0); bne t1, t2, loop; nop;
    const u32 entry = 0x80000400u;
    std::vector<u32> payload = {
        lui(8, 0x8000),
        ori(8, 8, 0x1000),
        addiu(9, 0, 0),         // t1 = 0
        addiu(10, 0, 1000),     // t2 = 1000
        // loop at offset 4*4:
        addiu(9, 9, 1),
        sw(9, 8, 0),
        bne(9, 10, -3),         // branch back to addiu
        nop(),
        beq(0, 0, -1),          // done loop
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

    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));

    const auto t0 = std::chrono::steady_clock::now();
    emu.run_cycles(100000);
    const auto t1 = std::chrono::steady_clock::now();

    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    EXPECT_GT(emu.cycles_executed(), 0u);
    EXPECT_EQ(emu.cpu().gpr(9), 1000u); // loop completed
    EXPECT_EQ(emu.bus().read32(0x1000), 1000u);

    // Verify reasonable throughput (> 0.5 MIPS in debug/RelWithDebInfo)
    if (ns > 0) {
        const double mips = (100000.0 / (ns / 1e9)) / 1e6;
        EXPECT_GT(mips, 0.5);
    }
}
