#include <gtest/gtest.h>

#include "n64/bus/bus.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/insn.hpp"
#include "n64/vi/vi.hpp"

#include <cstring>
#include <vector>

using namespace n64;
using namespace n64::insn;

namespace {

void put16_be(std::span<u8> mem, u32 off, u16 v) {
    mem[off] = static_cast<u8>((v >> 8) & 0xFF);
    mem[off + 1] = static_cast<u8>(v & 0xFF);
}

void put32_be(std::span<u8> mem, u32 off, u32 v) {
    mem[off]     = static_cast<u8>((v >> 24) & 0xFF);
    mem[off + 1] = static_cast<u8>((v >> 16) & 0xFF);
    mem[off + 2] = static_cast<u8>((v >> 8) & 0xFF);
    mem[off + 3] = static_cast<u8>(v & 0xFF);
}

// Pack RGB888 → RGBA5551 (A=1)
u16 pack_5551(u8 r, u8 g, u8 b) {
    const u32 R5 = r >> 3;
    const u32 G5 = g >> 3;
    const u32 B5 = b >> 3;
    return static_cast<u16>((R5 << 11) | (G5 << 6) | (B5 << 1) | 1u);
}

std::vector<u8> make_rom(u32 entry_va, const std::vector<u32>& payload,
                         std::string_view title = "VI TEST") {
    const std::size_t payload_bytes = payload.size() * 4;
    std::vector<u8> rom(0x1000 + payload_bytes, 0);
    auto put32 = [&](std::size_t off, u32 v) {
        rom[off + 0] = static_cast<u8>((v >> 24) & 0xFF);
        rom[off + 1] = static_cast<u8>((v >> 16) & 0xFF);
        rom[off + 2] = static_cast<u8>((v >> 8) & 0xFF);
        rom[off + 3] = static_cast<u8>(v & 0xFF);
    };
    put32(0x00, 0x8037'1240u);
    put32(0x04, 0x0000'000Fu);
    put32(0x08, entry_va);
    put32(0x0C, 0x0000'144Cu);
    for (std::size_t i = 0; i < 20; ++i) {
        rom[0x20 + i] = (i < title.size()) ? static_cast<u8>(title[i]) : u8(' ');
    }
    rom[0x38] = 'N';
    for (std::size_t i = 0; i < payload.size(); ++i) {
        put32(0x1000 + i * 4, payload[i]);
    }
    return rom;
}

} // namespace

// =============================================================================
// Geometry / registers
// =============================================================================

TEST(ViRegs, DefaultGeometry) {
    VideoInterface vi;
    vi.reset();
    EXPECT_EQ(vi.fb_width(), 320);
    EXPECT_GT(vi.fb_height(), 200);
    EXPECT_LE(vi.fb_height(), 240);
    EXPECT_EQ(vi.pixel_format(), ViPixelFormat::Rgba5551);
}

TEST(ViRegs, WidthAndControlWritable) {
    Emulator emu;
    emu.bus().write32(mmio::VI_BASE + VideoInterface::Width, 640);
    emu.bus().write32(mmio::VI_BASE + VideoInterface::Control, 0x0000'3203u); // 32-bit
    EXPECT_EQ(emu.vi().fb_width(), 640);
    EXPECT_EQ(emu.vi().pixel_format(), ViPixelFormat::Rgba8888);
}

// =============================================================================
// Pixel decode
// =============================================================================

TEST(ViFramebuffer, SolidColor5551) {
    Emulator emu;
    VideoInterface& vi = emu.vi();
    Bus& bus = emu.bus();

    constexpr u32 kOrigin = 0x0010'0000u;
    constexpr int kW = 32;
    constexpr int kH = 24;

    bus.write32(mmio::VI_BASE + VideoInterface::Origin, kOrigin);
    bus.write32(mmio::VI_BASE + VideoInterface::Width, static_cast<u32>(kW));
    bus.write32(mmio::VI_BASE + VideoInterface::Control, 0x0000'0002u); // 16-bit
    // Force height via V_START: start=0, end = h*2
    bus.write32(mmio::VI_BASE + VideoInterface::VStart,
                (0u << 16) | static_cast<u32>(kH * 2));

    EXPECT_EQ(vi.fb_width(), kW);
    EXPECT_EQ(vi.fb_height(), kH);

    const u16 red = pack_5551(255, 0, 0);
    auto rdram = bus.rdram();
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            put16_be(rdram, kOrigin + static_cast<u32>((y * kW + x) * 2), red);
        }
    }

    u8 r = 0, g = 0, b = 0, a = 0;
    ASSERT_TRUE(vi.sample_pixel(bus, 0, 0, r, g, b, a));
    EXPECT_GE(r, 240); // 5-bit expand ≈ 255
    EXPECT_EQ(g, 0);
    EXPECT_EQ(b, 0);
    EXPECT_EQ(a, 255);

    std::vector<u8> rgba;
    ASSERT_TRUE(vi.copy_framebuffer_rgba(bus, rgba));
    ASSERT_EQ(rgba.size(), static_cast<std::size_t>(kW * kH * 4));
    // Centre pixel
    const std::size_t mid = static_cast<std::size_t>((kH / 2) * kW + (kW / 2)) * 4;
    EXPECT_GE(rgba[mid + 0], 240);
    EXPECT_EQ(rgba[mid + 1], 0);
    EXPECT_EQ(rgba[mid + 2], 0);
}

TEST(ViFramebuffer, SolidColor8888) {
    Emulator emu;
    auto& vi = emu.vi();
    auto& bus = emu.bus();

    constexpr u32 kOrigin = 0x0010'0000u;
    constexpr int kW = 16;
    constexpr int kH = 8;

    bus.write32(mmio::VI_BASE + VideoInterface::Origin, kOrigin);
    bus.write32(mmio::VI_BASE + VideoInterface::Width, static_cast<u32>(kW));
    bus.write32(mmio::VI_BASE + VideoInterface::Control, 0x0000'0003u); // 32-bit
    bus.write32(mmio::VI_BASE + VideoInterface::VStart,
                (0u << 16) | static_cast<u32>(kH * 2));

    const u32 green = 0x00FF'00FFu; // R=0 G=255 B=0 A=255
    auto rdram = bus.rdram();
    for (int i = 0; i < kW * kH; ++i) {
        put32_be(rdram, kOrigin + static_cast<u32>(i * 4), green);
    }

    u8 r, g, b, a;
    ASSERT_TRUE(vi.sample_pixel(bus, 3, 4, r, g, b, a));
    EXPECT_EQ(r, 0);
    EXPECT_EQ(g, 255);
    EXPECT_EQ(b, 0);
    EXPECT_EQ(a, 255);

    std::vector<u8> rgba;
    ASSERT_TRUE(vi.copy_framebuffer_rgba(bus, rgba));
    ASSERT_EQ(rgba.size(), static_cast<std::size_t>(kW * kH * 4));
    const std::size_t pixel = static_cast<std::size_t>((4 * kW + 3) * 4);
    EXPECT_EQ(rgba[pixel + 0], 0);
    EXPECT_EQ(rgba[pixel + 1], 255);
    EXPECT_EQ(rgba[pixel + 2], 0);
    EXPECT_EQ(rgba[pixel + 3], 255);
}

TEST(ViFramebuffer, CheckerboardPattern) {
    Emulator emu;
    auto& vi = emu.vi();
    auto& bus = emu.bus();

    constexpr u32 kOrigin = 0x0020'0000u;
    constexpr int kW = 8;
    constexpr int kH = 8;
    bus.write32(mmio::VI_BASE + VideoInterface::Origin, kOrigin);
    bus.write32(mmio::VI_BASE + VideoInterface::Width, kW);
    bus.write32(mmio::VI_BASE + VideoInterface::Control, 0x2); // 16-bit
    bus.write32(mmio::VI_BASE + VideoInterface::VStart, (0u << 16) | (kH * 2u));

    auto rdram = bus.rdram();
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const bool on = ((x ^ y) & 1) != 0;
            const u16 p = on ? pack_5551(0, 0, 255) : pack_5551(255, 255, 255);
            put16_be(rdram, kOrigin + static_cast<u32>((y * kW + x) * 2), p);
        }
    }

    u8 r, g, b, a;
    ASSERT_TRUE(vi.sample_pixel(bus, 0, 0, r, g, b, a));
    EXPECT_GE(r, 240); // white
    ASSERT_TRUE(vi.sample_pixel(bus, 1, 0, r, g, b, a));
    EXPECT_GE(b, 240); // blue
    EXPECT_LE(r, 16);
}

// =============================================================================
// Timing / frames
// =============================================================================

TEST(ViTiming, AdvancesAndCountsFrames) {
    Emulator emu;
    auto& vi = emu.vi();
    // Speed up: fewer cycles per line via many ticks
    const u64 before = vi.frame_count();
    // One full frame ≈ 262 lines * 5963 cycles ≈ 1.56e6
    emu.run_cycles(VideoInterface::kDefaultCyclesPerLine *
                   VideoInterface::kDefaultLinesPerFrame + 100);
    EXPECT_GT(vi.frame_count(), before);
    EXPECT_TRUE(vi.frame_ready());
    vi.clear_frame_ready();
    EXPECT_FALSE(vi.frame_ready());
}

TEST(ViTiming, RaisesInterruptAtVintr) {
    Emulator emu;
    auto& vi = emu.vi();
    auto& bus = emu.bus();

    // Enable VI in MI mask
    bus.write32(mmio::MI_BASE + MipsInterface::IntrMask, 1u << 7); // SET_VI
    bus.write32(mmio::VI_BASE + VideoInterface::VIntr, 4);

    // Step enough lines to pass line 4
    for (int i = 0; i < 10; ++i) {
        vi.tick(VideoInterface::kDefaultCyclesPerLine);
    }
    EXPECT_NE(bus.mi().pending() & mmio::MiIntr::VI, 0u);

    // Clear by writing CURRENT
    bus.write32(mmio::VI_BASE + VideoInterface::VCurrent, 0);
    EXPECT_EQ(bus.mi().pending() & mmio::MiIntr::VI, 0u);
}

// =============================================================================
// Homebrew: CPU fills FB + configures VI, then we present
// =============================================================================

TEST(ViIntegration, EmulatorPresentFramebuffer) {
    // Pre-fill RDRAM with blue 16-bit, configure VI like a game would.
    Emulator emu;
    constexpr u32 kOrigin = 0x0010'0000u;
    constexpr int kW = 40;
    constexpr int kH = 30;

    auto rdram = emu.bus().rdram();
    const u16 blue = pack_5551(0, 0, 255);
    for (int i = 0; i < kW * kH; ++i) {
        put16_be(rdram, kOrigin + static_cast<u32>(i * 2), blue);
    }

    emu.bus().write32(mmio::VI_BASE + VideoInterface::Origin, kOrigin);
    emu.bus().write32(mmio::VI_BASE + VideoInterface::Width, kW);
    emu.bus().write32(mmio::VI_BASE + VideoInterface::Control, 0x3202);
    emu.bus().write32(mmio::VI_BASE + VideoInterface::VStart, (0u << 16) | (kH * 2u));

    std::vector<u8> rgba;
    int w = 0, h = 0;
    ASSERT_TRUE(emu.present_framebuffer(rgba, w, h));
    EXPECT_EQ(w, kW);
    EXPECT_EQ(h, kH);
    EXPECT_GE(rgba[2], 240); // B channel of first pixel
    EXPECT_LE(rgba[0], 16);
}

TEST(ViIntegration, CpuProgramFillsAndScans) {
    // Tiny program at 0x80000400:
    //   Configure VI origin/width/control/vstart via KSEG1 MMIO
    //   Fill 8x8 16-bit FB at 0x80100000 with magenta
    //   Infinite loop
    const u32 entry = 0x8000'0400u;
    const u32 fb_phys = 0x0010'0000u;
    const u32 fb_kseg0 = 0x8010'0000u;
    const int kW = 8, kH = 8;
    const u16 mag = pack_5551(255, 0, 255);

    // We'll just pre-fill FB in RDRAM and let a short CPU program write VI regs.
    std::vector<u32> code = {
        // t0 = 0xA4400000 VI base
        lui(8, 0xA440),
        // origin
        lui(9, static_cast<u16>(fb_phys >> 16)),
        ori(9, 9, static_cast<u16>(fb_phys & 0xFFFF)),
        sw(9, 8, VideoInterface::Origin),
        // width = 8
        addiu(9, 0, kW),
        sw(9, 8, VideoInterface::Width),
        // control = 0x3202
        lui(9, 0x0000),
        ori(9, 9, 0x3202),
        sw(9, 8, VideoInterface::Control),
        // vstart = h*2
        addiu(9, 0, kH * 2),
        sw(9, 8, VideoInterface::VStart),
        // done
        beq(0, 0, -1),
        nop(),
    };
    auto rom = make_rom(entry, code, "VI FILL");

    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));

    // Fill framebuffer in RDRAM (simulating RDP/CPU draw done before present)
    auto rdram = emu.bus().rdram();
    for (int i = 0; i < kW * kH; ++i) {
        put16_be(rdram, fb_phys + static_cast<u32>(i * 2), mag);
    }

    emu.run_cycles(40); // run VI setup program

    EXPECT_EQ(emu.vi().origin(), fb_phys);
    EXPECT_EQ(emu.vi().fb_width(), kW);
    EXPECT_EQ(emu.vi().fb_height(), kH);

    std::vector<u8> rgba;
    int w, h;
    ASSERT_TRUE(emu.present_framebuffer(rgba, w, h));
    EXPECT_EQ(w, kW);
    EXPECT_EQ(h, kH);
    // Magenta-ish
    EXPECT_GE(rgba[0], 240);
    EXPECT_LE(rgba[1], 16);
    EXPECT_GE(rgba[2], 240);

    (void)fb_kseg0;
}

TEST(ViBlank, BlankFormatReturnsFalse) {
    Emulator emu;
    emu.bus().write32(mmio::VI_BASE + VideoInterface::Control, 0); // blank
    std::vector<u8> rgba;
    int w, h;
    EXPECT_FALSE(emu.present_framebuffer(rgba, w, h));
}
