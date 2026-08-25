#include <gtest/gtest.h>

#include "n64/ai/ai.hpp"
#include "n64/bus/bus.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/cpu/insn.hpp"

#include <cmath>
#include <vector>

using namespace n64;
using namespace n64::insn;

namespace {

void put_s16_be(std::span<u8> mem, u32 addr, s16 v) {
    mem[addr]     = static_cast<u8>((static_cast<u16>(v) >> 8) & 0xFF);
    mem[addr + 1] = static_cast<u8>(static_cast<u16>(v) & 0xFF);
}

void put_stereo_be(std::span<u8> mem, u32 addr, s16 l, s16 r) {
    put_s16_be(mem, addr, l);
    put_s16_be(mem, addr + 2, r);
}

std::vector<u8> make_rom(u32 entry, const std::vector<u32>& code) {
    std::vector<u8> rom(0x1000 + code.size() * 4, 0);
    auto put32 = [&](std::size_t o, u32 v) {
        rom[o] = static_cast<u8>(v >> 24);
        rom[o + 1] = static_cast<u8>(v >> 16);
        rom[o + 2] = static_cast<u8>(v >> 8);
        rom[o + 3] = static_cast<u8>(v);
    };
    put32(0x00, 0x80371240);
    put32(0x08, entry);
    rom[0x20] = 'A'; rom[0x21] = 'I';
    rom[0x3E] = 'E';
    for (std::size_t i = 0; i < code.size(); ++i) {
        put32(0x1000 + i * 4, code[i]);
    }
    return rom;
}

} // namespace

// =============================================================================
// Registers / rate
// =============================================================================

TEST(AiRegs, DefaultSampleRate) {
    Emulator emu;
    const u32 rate = emu.ai().sample_rate();
    EXPECT_GE(rate, 8000u);
    EXPECT_LE(rate, 96000u);
}

TEST(AiRegs, DacRateChangesSampleRate) {
    Emulator emu;
    // dacrate = clock/rate - 1 → request ~16 kHz
    const u32 dac = AudioInterface::kDacClockHz / 16000u - 1u;
    emu.bus().write32(mmio::AI_BASE + AudioInterface::DacRate, dac);
    const u32 rate = emu.ai().sample_rate();
    EXPECT_NEAR(static_cast<double>(rate), 16000.0, 500.0);
}

// =============================================================================
// DMA + samples
// =============================================================================

TEST(AiDma, InstantCompletePushesSamples) {
    Emulator emu;
    auto& ai = emu.ai();
    auto rdram = emu.bus().rdram();

    // 8 stereo frames of a known pattern
    constexpr u32 kAddr = 0x2000;
    constexpr u32 kFrames = 8;
    for (u32 i = 0; i < kFrames; ++i) {
        put_stereo_be(rdram, kAddr + i * 4,
                      static_cast<s16>(1000 + static_cast<int>(i)),
                      static_cast<s16>(-1000 - static_cast<int>(i)));
    }

    emu.bus().write32(mmio::AI_BASE + AudioInterface::Control, 1);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::DramAddr, kAddr);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Len, kFrames * 4);

    EXPECT_TRUE(ai.dma_busy());
    ai.complete_dma();
    EXPECT_FALSE(ai.dma_busy());
    EXPECT_EQ(ai.dmas_completed(), 1u);
    EXPECT_EQ(ai.samples_pushed(), kFrames);
    EXPECT_NE(emu.bus().mi().pending() & mmio::MiIntr::AI, 0u);

    std::vector<s16> out(kFrames * 2);
    const std::size_t got = ai.pull_frames(out);
    EXPECT_EQ(got, kFrames);
    EXPECT_EQ(out[0], 1000);
    EXPECT_EQ(out[1], -1000);
    EXPECT_EQ(out[14], static_cast<s16>(1000 + 7));
    EXPECT_EQ(out[15], static_cast<s16>(-1000 - 7));
}

TEST(AiDma, TickConsumesOverTime) {
    Emulator emu;
    auto& ai = emu.ai();
    auto rdram = emu.bus().rdram();

    constexpr u32 kAddr = 0x3000;
    constexpr u32 kFrames = 32;
    for (u32 i = 0; i < kFrames; ++i) {
        put_stereo_be(rdram, kAddr + i * 4, 100, -100);
    }

    // Force a known rate via DACRATE
    const u32 dac = AudioInterface::kDacClockHz / 32000u - 1u;
    emu.bus().write32(mmio::AI_BASE + AudioInterface::DacRate, dac);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Control, 1);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::DramAddr, kAddr);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Len, kFrames * 4);

    EXPECT_TRUE(ai.dma_busy());
    EXPECT_EQ(ai.samples_pushed(), 0u);

    // One frame worth of CPU cycles should produce ~1 sample.
    const Cycles cpf = kCpuClockHz / 32000;
    ai.tick(cpf);
    EXPECT_GE(ai.samples_pushed(), 1u);
    EXPECT_TRUE(ai.dma_busy());

    // Finish the rest.
    ai.tick(cpf * (kFrames + 2));
    EXPECT_FALSE(ai.dma_busy());
    EXPECT_EQ(ai.samples_pushed(), kFrames);
    EXPECT_EQ(ai.dmas_completed(), 1u);
}

TEST(AiDma, QueueSecondDma) {
    Emulator emu;
    auto& ai = emu.ai();
    auto rdram = emu.bus().rdram();

    for (u32 i = 0; i < 16; ++i) {
        put_stereo_be(rdram, 0x4000 + i * 4, 1, 1);
        put_stereo_be(rdram, 0x5000 + i * 4, 2, 2);
    }

    emu.bus().write32(mmio::AI_BASE + AudioInterface::Control, 1);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::DramAddr, 0x4000);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Len, 16 * 4);
    EXPECT_TRUE(ai.dma_busy());
    EXPECT_FALSE(ai.dma_full());

    // Queue second
    emu.bus().write32(mmio::AI_BASE + AudioInterface::DramAddr, 0x5000);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Len, 16 * 4);
    EXPECT_TRUE(ai.dma_full());

    ai.complete_dma(); // finish first → should auto-start second
    EXPECT_TRUE(ai.dma_busy());
    EXPECT_FALSE(ai.dma_full());
    EXPECT_EQ(ai.dmas_completed(), 1u);

    ai.complete_dma();
    EXPECT_FALSE(ai.dma_busy());
    EXPECT_EQ(ai.dmas_completed(), 2u);
    EXPECT_EQ(ai.samples_pushed(), 32u);
}

TEST(AiDma, StatusClearIrq) {
    Emulator emu;
    auto rdram = emu.bus().rdram();
    put_stereo_be(rdram, 0x1000, 0, 0);
    emu.bus().write32(mmio::MI_BASE + MipsInterface::IntrMask, 1u << 5);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Control, 1);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::DramAddr, 0x1000);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Len, 8);
    ASSERT_TRUE(emu.ai().dma_busy());
    emu.ai().complete_dma();
    EXPECT_NE(emu.bus().mi().pending() & mmio::MiIntr::AI, 0u);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Status, 0);
    EXPECT_EQ(emu.bus().mi().pending() & mmio::MiIntr::AI, 0u);
}

// =============================================================================
// Emulator integration (tick from run_cycles)
// =============================================================================

TEST(AiIntegration, RunCyclesProducesSamples) {
    Emulator emu;
    auto rdram = emu.bus().rdram();

    // 64 frames of a crude square wave
    constexpr u32 kAddr = 0x6000;
    constexpr u32 kFrames = 64;
    for (u32 i = 0; i < kFrames; ++i) {
        const s16 s = (i & 4) ? static_cast<s16>(8000) : static_cast<s16>(-8000);
        put_stereo_be(rdram, kAddr + i * 4, s, s);
    }

    const u32 dac = AudioInterface::kDacClockHz / 16000u - 1u;
    emu.bus().write32(mmio::AI_BASE + AudioInterface::DacRate, dac);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Control, 1);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::DramAddr, kAddr);
    emu.bus().write32(mmio::AI_BASE + AudioInterface::Len, kFrames * 4);

    // Enough cycles for all frames at 16 kHz: cpu_hz/16000 * 64
    const Cycles need = (kCpuClockHz / 16000) * (kFrames + 4);
    emu.run_cycles(need);

    EXPECT_GE(emu.ai().samples_pushed(), kFrames);
    EXPECT_GE(emu.ai().dmas_completed(), 1u);

    std::vector<s16> out(kFrames * 2);
    const std::size_t got = emu.ai().pull_frames(out);
    EXPECT_GE(got, 8u);
    // Should contain both polarities of the square wave.
    bool pos = false, neg = false;
    for (std::size_t i = 0; i < got * 2; ++i) {
        if (out[i] > 1000) pos = true;
        if (out[i] < -1000) neg = true;
    }
    EXPECT_TRUE(pos);
    EXPECT_TRUE(neg);
}

TEST(AiIntegration, CpuProgramStartsDma) {
    // CPU writes AI regs then spins; samples come from prefilled RDRAM.
    const u32 entry = 0x8000'0400u;
    std::vector<u32> code = {
        // t0 = AI base 0xA4500000
        lui(8, 0xA450),
        // DRAM_ADDR = 0x7000
        lui(9, 0x0000),
        ori(9, 9, 0x7000),
        sw(9, 8, AudioInterface::DramAddr),
        // CONTROL = 1
        addiu(9, 0, 1),
        sw(9, 8, AudioInterface::Control),
        // LEN = 64 bytes (16 frames)
        addiu(9, 0, 64),
        sw(9, 8, AudioInterface::Len),
        beq(0, 0, -1),
        nop(),
    };
    auto rom = make_rom(entry, code);
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));

    auto rdram = emu.bus().rdram();
    for (u32 i = 0; i < 16; ++i) {
        put_stereo_be(rdram, 0x7000 + i * 4, 1234, -1234);
    }

    // Run CPU setup + enough AI ticks
    emu.run_cycles(50); // kick DMA via program
    EXPECT_TRUE(emu.ai().dma_busy() || emu.ai().dmas_completed() > 0);

    emu.run_cycles((kCpuClockHz / 32000) * 32);
    EXPECT_GE(emu.ai().samples_pushed(), 1u);
}

TEST(AiPull, EmptyRingReturnsZero) {
    Emulator emu;
    std::array<s16, 32> out{};
    EXPECT_EQ(emu.ai().pull_frames(out), 0u);
    EXPECT_EQ(emu.ai().buffered_frames(), 0u);
}
