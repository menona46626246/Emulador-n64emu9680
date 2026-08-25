#include <gtest/gtest.h>

#include "n64/bus/bus.hpp"
#include "n64/bus/dp_regs.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/insn.hpp"
#include "n64/rcp/rdp/commands.hpp"
#include "n64/rcp/rdp/rdp.hpp"
#include "n64/vi/vi.hpp"

#include <vector>

using namespace n64;
using namespace n64::rdp_cmd;
using namespace n64::insn;

namespace {

u16 pack_5551(u8 r, u8 g, u8 b) {
    return static_cast<u16>(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1);
}

u16 read_pix16(Bus& bus, u32 addr) {
    return bus.read16(addr);
}

void write_cmd_list(Bus& bus, u32 base, std::initializer_list<u64> cmds) {
    auto rdram = bus.rdram();
    u32 off = base;
    for (u64 c : cmds) {
        store_be64_rdram(rdram, off, c);
        off += 8;
    }
}

void write_cmd_list_pairs(Bus& bus, u32 base, const std::vector<u64>& words) {
    auto rdram = bus.rdram();
    u32 off = base;
    for (u64 c : words) {
        store_be64_rdram(rdram, off, c);
        off += 8;
    }
}

void put16_be(std::span<u8> mem, u32 off, u16 v) {
    mem[off] = static_cast<u8>((v >> 8) & 0xFF);
    mem[off + 1] = static_cast<u8>(v & 0xFF);
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
    rom[0x20] = 'R'; rom[0x21] = 'D'; rom[0x22] = 'P';
    rom[0x3E] = 'E';
    for (std::size_t i = 0; i < code.size(); ++i) {
        put32(0x1000 + i * 4, code[i]);
    }
    return rom;
}

} // namespace

// =============================================================================
// Direct RDP API
// =============================================================================

TEST(RdpDirect, FillRect16Solid) {
    Emulator emu;
    auto& rdp = emu.rdp();
    auto& bus = emu.bus();

    constexpr u32 kFb = 0x0010'0000u;
    constexpr u32 kW = 16;
    constexpr u32 kH = 8;

    // Clear FB to black
    std::fill(bus.rdram().begin() + kFb,
              bus.rdram().begin() + kFb + kW * kH * 2, 0);

    const u16 red = pack_5551(255, 0, 0);
    rdp.execute_command(set_color_image(kFb, kW, /*size=*/2));
    rdp.execute_command(set_scissor(0, 0, static_cast<int>(kW), static_cast<int>(kH)));
    rdp.execute_command(set_other_modes_fill());
    rdp.execute_command(set_fill_color(fill_color_16(red)));
    rdp.execute_command(fill_rectangle(0, 0, static_cast<int>(kW), static_cast<int>(kH)));
    rdp.execute_command(sync_full());

    EXPECT_GT(rdp.fill_pixels(), 0u);
    EXPECT_EQ(read_pix16(bus, kFb), red);
    EXPECT_EQ(read_pix16(bus, kFb + (kW * kH - 1) * 2), red);
    // DP interrupt raised by SyncFull
    EXPECT_NE(bus.mi().pending() & mmio::MiIntr::DP, 0u);
}

TEST(RdpDirect, FillRect32Solid) {
    Emulator emu;
    auto& rdp = emu.rdp();
    auto& bus = emu.bus();

    constexpr u32 kFb = 0x0010'0000u;
    constexpr u32 kW = 8;
    const u32 green = 0x00FF'00FFu;

    rdp.execute_command(set_color_image(kFb, kW, /*size=*/3));
    rdp.execute_command(set_scissor(0, 0, 8, 4));
    rdp.execute_command(set_fill_color(green));
    rdp.execute_command(fill_rectangle(0, 0, 8, 4));

    EXPECT_EQ(bus.read32(kFb), green);
    EXPECT_EQ(bus.read32(kFb + 4), green);
}

TEST(RdpDirect, FillRect16UsesBothHalvesOfFillWord) {
    Emulator emu;
    auto& rdp = emu.rdp();
    auto& bus = emu.bus();

    constexpr u32 kFb = 0x0010'0000u;
    constexpr u16 kFirst = 0xF801u;
    constexpr u16 kSecond = 0x07C1u;

    rdp.execute_command(set_color_image(kFb, 2, /*size=*/2));
    rdp.execute_command(set_scissor(0, 0, 2, 1));
    rdp.execute_command(set_other_modes_fill());
    rdp.execute_command(set_fill_color((static_cast<u32>(kFirst) << 16) | kSecond));
    rdp.execute_command(fill_rectangle(0, 0, 2, 1));

    EXPECT_EQ(read_pix16(bus, kFb), kFirst);
    EXPECT_EQ(read_pix16(bus, kFb + 2), kSecond);
}

TEST(RdpDirect, ScissorClipsFill) {
    Emulator emu;
    auto& rdp = emu.rdp();
    auto& bus = emu.bus();

    constexpr u32 kFb = 0x0010'0000u;
    constexpr u32 kW = 16;
    std::fill(bus.rdram().begin() + kFb,
              bus.rdram().begin() + kFb + kW * 16 * 2, 0);

    const u16 blue = pack_5551(0, 0, 255);
    rdp.execute_command(set_color_image(kFb, kW, 2));
    // Scissor only left 4x4
    rdp.execute_command(set_scissor(0, 0, 4, 4));
    rdp.execute_command(set_fill_color(fill_color_16(blue)));
    // Fill a larger rect — should clip
    rdp.execute_command(fill_rectangle(0, 0, 16, 16));

    EXPECT_EQ(read_pix16(bus, kFb), blue);                 // (0,0) inside
    EXPECT_EQ(read_pix16(bus, kFb + 3 * 2), blue);         // (3,0) inside
    EXPECT_EQ(read_pix16(bus, kFb + 4 * 2), 0);            // (4,0) outside
    EXPECT_EQ(read_pix16(bus, kFb + kW * 4 * 2), 0);       // (0,4) outside
}

TEST(RdpDirect, PartialRectangle) {
    Emulator emu;
    auto& rdp = emu.rdp();
    auto& bus = emu.bus();

    constexpr u32 kFb = 0x0020'0000u;
    constexpr u32 kW = 32;
    std::fill(bus.rdram().begin() + kFb,
              bus.rdram().begin() + kFb + kW * 32 * 2, 0);

    const u16 white = pack_5551(255, 255, 255);
    rdp.execute_command(set_color_image(kFb, kW, 2));
    rdp.execute_command(set_scissor(0, 0, 32, 32));
    rdp.execute_command(set_fill_color(fill_color_16(white)));
    // 8x8 block at (4,4)
    rdp.execute_command(fill_rectangle(4, 4, 12, 12));

    EXPECT_EQ(read_pix16(bus, kFb), 0); // (0,0) empty
    EXPECT_EQ(read_pix16(bus, kFb + (4 * kW + 4) * 2), white);
    EXPECT_EQ(read_pix16(bus, kFb + (11 * kW + 11) * 2), white);
    EXPECT_EQ(read_pix16(bus, kFb + (12 * kW + 4) * 2), 0);
}

// =============================================================================
// DP command list in RDRAM
// =============================================================================

TEST(RdpDpRegs, CommandListViaEndWrite) {
    Emulator emu;
    auto& bus = emu.bus();

    constexpr u32 kFb = 0x0010'0000u;
    constexpr u32 kList = 0x0002'0000u;
    constexpr u32 kW = 20;
    const u16 red = pack_5551(255, 0, 0);

    std::fill(bus.rdram().begin() + kFb,
              bus.rdram().begin() + kFb + kW * 10 * 2, 0);

    write_cmd_list(bus, kList, {
        set_color_image(kFb, kW, 2),
        set_scissor(0, 0, static_cast<int>(kW), 10),
        set_other_modes_fill(),
        set_fill_color(fill_color_16(red)),
        fill_rectangle(0, 0, static_cast<int>(kW), 10),
        sync_full(),
    });
    const u32 list_end = kList + 6 * 8;

    // Kick RDP via DP_START / DP_END
    bus.write32(mmio::DP_CMD_BASE + DpRegisters::Start, kList);
    bus.write32(mmio::DP_CMD_BASE + DpRegisters::End, list_end);

    EXPECT_EQ(read_pix16(bus, kFb), red);
    EXPECT_EQ(read_pix16(bus, kFb + (kW * 5 + 10) * 2), red);
    EXPECT_NE(bus.mi().pending() & mmio::MiIntr::DP, 0u);
    EXPECT_GT(emu.rdp().commands_executed(), 0u);
}

// =============================================================================
// Integration: RDP fill + VI present
// =============================================================================

TEST(RdpViIntegration, FillThenPresent) {
    Emulator emu;
    auto& bus = emu.bus();
    auto& rdp = emu.rdp();

    constexpr u32 kFb = 0x0010'0000u;
    constexpr int kW = 40;
    constexpr int kH = 30;
    const u16 blue = pack_5551(0, 0, 255);

    rdp.execute_command(set_color_image(kFb, static_cast<u32>(kW), 2));
    rdp.execute_command(set_scissor(0, 0, kW, kH));
    rdp.execute_command(set_fill_color(fill_color_16(blue)));
    rdp.execute_command(fill_rectangle(0, 0, kW, kH));

    // Point VI at the same FB
    bus.write32(mmio::VI_BASE + VideoInterface::Origin, kFb);
    bus.write32(mmio::VI_BASE + VideoInterface::Width, static_cast<u32>(kW));
    bus.write32(mmio::VI_BASE + VideoInterface::Control, 0x3202);
    bus.write32(mmio::VI_BASE + VideoInterface::VStart,
                (0u << 16) | static_cast<u32>(kH * 2));

    std::vector<u8> rgba;
    int w = 0, h = 0;
    ASSERT_TRUE(emu.present_framebuffer(rgba, w, h));
    EXPECT_EQ(w, kW);
    EXPECT_EQ(h, kH);
    // Blue-ish
    EXPECT_LE(rgba[0], 16);
    EXPECT_LE(rgba[1], 16);
    EXPECT_GE(rgba[2], 240);
}

// =============================================================================
// CPU program builds a tiny command list and kicks DP
// =============================================================================

TEST(RdpIntegration, CpuKicksDpFill) {
    // Pre-build command list in RDRAM after boot, then CPU only writes DP regs.
    // (Building 64-bit BE words from pure MIPS is verbose; pre-place list.)
    const u32 entry = 0x8000'0400u;
    std::vector<u32> code = {
        // t0 = DP base 0xA4100000
        lui(8, 0xA410),
        // t1 = list phys 0x20000
        lui(9, 0x0002),
        sw(9, 8, DpRegisters::Start),
        // t1 = end = 0x20000 + 48 = 0x20030
        ori(9, 9, 0x0030),
        sw(9, 8, DpRegisters::End),
        beq(0, 0, -1),
        nop(),
    };
    auto rom = make_rom(entry, code);

    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));

    constexpr u32 kFb = 0x0010'0000u;
    constexpr u32 kList = 0x0002'0000u;
    constexpr u32 kW = 16;
    const u16 mag = pack_5551(255, 0, 255);

    write_cmd_list(emu.bus(), kList, {
        set_color_image(kFb, kW, 2),
        set_scissor(0, 0, 16, 8),
        set_fill_color(fill_color_16(mag)),
        fill_rectangle(0, 0, 16, 8),
        sync_pipe(),
        sync_full(),
    });

    emu.run_cycles(30);

    EXPECT_EQ(read_pix16(emu.bus(), kFb), mag);
    EXPECT_NE(emu.bus().mi().pending() & mmio::MiIntr::DP, 0u);
}

// =============================================================================
// Texture rectangle (Phase 6+)
// =============================================================================

TEST(RdpTex, LoadBlockAndTexRect) {
    Emulator emu;
    auto& rdp = emu.rdp();
    auto& bus = emu.bus();

    constexpr u32 kFb  = 0x0010'0000u;
    constexpr u32 kTex = 0x0020'0000u;
    constexpr u32 kW = 32;
    constexpr u32 kH = 16;
    constexpr u32 kTw = 8; // texture 8x8

    // Build a simple 8x8 texture: left half red, right half green
    const u16 red = pack_5551(255, 0, 0);
    const u16 green = pack_5551(0, 255, 0);
    auto rdram = bus.rdram();
    for (u32 y = 0; y < kTw; ++y) {
        for (u32 x = 0; x < kTw; ++x) {
            put16_be(rdram, kTex + (y * kTw + x) * 2, (x < 4) ? red : green);
        }
    }
    std::fill(rdram.begin() + kFb, rdram.begin() + kFb + kW * kH * 2, 0);

    // line words for 8 texels * 2 bytes = 16 bytes = 2 * 64-bit words
    const u16 line_words = 2;

    rdp.execute_command(set_color_image(kFb, kW, 2));
    rdp.execute_command(set_scissor(0, 0, static_cast<int>(kW), static_cast<int>(kH)));
    rdp.execute_command(set_texture_image(kTex, kTw, 2));
    rdp.execute_command(set_tile(0, /*size=*/2, line_words, /*tmem=*/0));
    rdp.execute_command(set_tile_size(0, 0, 0, static_cast<int>(kTw - 1),
                                      static_cast<int>(kTw - 1)));
    rdp.execute_command(load_tile(0, 0, 0, static_cast<int>(kTw - 1),
                                  static_cast<int>(kTw - 1)));
    rdp.execute_command(set_other_modes_copy());

    // Blit texture to FB at (4,4) size 8x8, 1:1
    auto [hi, lo] = texture_rectangle(4, 4, 12, 12, 0, 0, 0, 1, 1);
    rdp.execute_command_pair(hi, lo);
    rdp.execute_command(sync_full());

    EXPECT_GT(rdp.tex_pixels(), 0u);
    // (4,4) should be red (left half of tex)
    EXPECT_EQ(read_pix16(bus, kFb + (4 * kW + 4) * 2), red);
    // (10,4) should be green (right half)
    EXPECT_EQ(read_pix16(bus, kFb + (4 * kW + 10) * 2), green);
    // Outside blit remains 0
    EXPECT_EQ(read_pix16(bus, kFb), 0);
}

TEST(RdpTex, LoadBlockLinear) {
    Emulator emu;
    auto& rdp = emu.rdp();
    auto& bus = emu.bus();

    constexpr u32 kFb  = 0x0010'0000u;
    constexpr u32 kTex = 0x0030'0000u;
    const u16 blue = pack_5551(0, 0, 255);
    const u16 yellow = pack_5551(255, 255, 0);

    // 4 texels linear
    auto rdram = bus.rdram();
    put16_be(rdram, kTex + 0, blue);
    put16_be(rdram, kTex + 2, blue);
    put16_be(rdram, kTex + 4, yellow);
    put16_be(rdram, kTex + 6, yellow);
    std::fill(rdram.begin() + kFb, rdram.begin() + kFb + 64, 0);

    rdp.execute_command(set_color_image(kFb, 16, 2));
    rdp.execute_command(set_scissor(0, 0, 16, 4));
    rdp.execute_command(set_texture_image(kTex, 4, 2));
    rdp.execute_command(set_tile(0, 2, /*line=*/1, /*tmem=*/0));
    // LoadBlock: sl=0 sh=3 → 4 texels
    rdp.execute_command(load_block(0, 0, 0, 3));
    rdp.execute_command(set_other_modes_copy());

    auto [hi, lo] = texture_rectangle(0, 0, 4, 1, 0, 0, 0, 1, 1);
    rdp.execute_command_pair(hi, lo);

    EXPECT_EQ(read_pix16(bus, kFb + 0), blue);
    EXPECT_EQ(read_pix16(bus, kFb + 2), blue);
    EXPECT_EQ(read_pix16(bus, kFb + 4), yellow);
    EXPECT_EQ(read_pix16(bus, kFb + 6), yellow);
}

TEST(RdpTex, TexRectClippedByScissor) {
    Emulator emu;
    auto& rdp = emu.rdp();
    auto& bus = emu.bus();

    constexpr u32 kFb = 0x0010'0000u;
    constexpr u32 kTex = 0x0020'0000u;
    const u16 white = pack_5551(255, 255, 255);
    auto rdram = bus.rdram();
    for (int i = 0; i < 16; ++i) {
        put16_be(rdram, kTex + static_cast<u32>(i) * 2, white);
    }
    std::fill(rdram.begin() + kFb, rdram.begin() + kFb + 32 * 32 * 2, 0);

    rdp.execute_command(set_color_image(kFb, 32, 2));
    rdp.execute_command(set_scissor(0, 0, 4, 4)); // tight scissor
    rdp.execute_command(set_texture_image(kTex, 4, 2));
    rdp.execute_command(set_tile(0, 2, 1, 0));
    rdp.execute_command(load_block(0, 0, 0, 15)); // 16 texels
    // Try to draw 8x8 — only 4x4 should land
    auto [hi, lo] = texture_rectangle(0, 0, 8, 8, 0, 0, 0, 1, 1);
    rdp.execute_command_pair(hi, lo);

    EXPECT_EQ(read_pix16(bus, kFb), white);
    EXPECT_EQ(read_pix16(bus, kFb + (3 * 32 + 3) * 2), white);
    EXPECT_EQ(read_pix16(bus, kFb + (4 * 32 + 0) * 2), 0);
    EXPECT_EQ(read_pix16(bus, kFb + (0 * 32 + 4) * 2), 0);
}

TEST(RdpTex, CommandListWithTexRect) {
    Emulator emu;
    auto& bus = emu.bus();
    constexpr u32 kFb = 0x0010'0000u;
    constexpr u32 kTex = 0x0020'0000u;
    constexpr u32 kList = 0x0002'0000u;
    const u16 cyan = pack_5551(0, 255, 255);

    auto rdram = bus.rdram();
    for (int i = 0; i < 4 * 4; ++i) {
        put16_be(rdram, kTex + static_cast<u32>(i) * 2, cyan);
    }
    std::fill(rdram.begin() + kFb, rdram.begin() + kFb + 64 * 2, 0);

    auto [thi, tlo] = texture_rectangle(0, 0, 4, 4, 0, 0, 0, 1, 1);
    std::vector<u64> words = {
        set_color_image(kFb, 16, 2),
        set_scissor(0, 0, 16, 16),
        set_texture_image(kTex, 4, 2),
        set_tile(0, 2, 1, 0),
        load_tile(0, 0, 0, 3, 3),
        set_other_modes_copy(),
        thi, tlo,
        sync_full(),
    };
    write_cmd_list_pairs(bus, kList, words);
    const u32 end = kList + static_cast<u32>(words.size()) * 8;

    bus.write32(mmio::DP_CMD_BASE + DpRegisters::Start, kList);
    bus.write32(mmio::DP_CMD_BASE + DpRegisters::End, end);

    EXPECT_EQ(read_pix16(bus, kFb), cyan);
    EXPECT_EQ(read_pix16(bus, kFb + (3 * 16 + 3) * 2), cyan);
    EXPECT_GT(emu.rdp().tex_pixels(), 0u);
}
