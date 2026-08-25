#include <gtest/gtest.h>

#include "n64/bus/bus.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/bus/pi.hpp"
#include "n64/cart/header.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/cpu/insn.hpp"
#include "n64/pif/boot.hpp"
#include "n64/pif/pif.hpp"
#include "n64/rcp/rsp/rsp.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace n64;
using namespace n64::insn;

namespace {

/// Build a minimal .z64-like image:
///   [0x0000, 0x0040) header
///   [0x0040, 0x1000) sparse IPL3 stub (zeros → CIC 6102)
///   [0x1000, ...)     payload (code + data), loaded at entrypoint
std::vector<u8> make_rom(u32 entry_va,
                         const std::vector<u32>& payload_words,
                         std::string_view title = "N64EMU TESTROM",
                         char country = 'E') {
    const std::size_t payload_bytes = payload_words.size() * 4;
    std::vector<u8> rom(0x1000 + payload_bytes, 0);

    auto put32 = [&](std::size_t off, u32 v) {
        rom[off + 0] = static_cast<u8>((v >> 24) & 0xFF);
        rom[off + 1] = static_cast<u8>((v >> 16) & 0xFF);
        rom[off + 2] = static_cast<u8>((v >> 8) & 0xFF);
        rom[off + 3] = static_cast<u8>(v & 0xFF);
    };

    // PI BSD DOM1 / magic
    put32(0x00, 0x8037'1240u);
    put32(0x04, 0x0000'000Fu); // clock
    put32(0x08, entry_va);     // PC
    put32(0x0C, 0x0000'144Cu); // release
    put32(0x10, 0xDEAD'BEEFu); // crc1 placeholder
    put32(0x14, 0xCAFE'BABEu); // crc2 placeholder

    // Title at 0x20 (20 bytes)
    for (std::size_t i = 0; i < 20; ++i) {
        rom[0x20 + i] = (i < title.size()) ? static_cast<u8>(title[i]) : static_cast<u8>(' ');
    }
    rom[0x38] = 'N';
    rom[0x3C] = 'E';
    rom[0x3D] = 'D';
    rom[0x3E] = static_cast<u8>(country);
    rom[0x3F] = 0;

    // Payload at 0x1000
    for (std::size_t i = 0; i < payload_words.size(); ++i) {
        put32(0x1000 + i * 4, payload_words[i]);
    }
    return rom;
}

} // namespace

// =============================================================================
// Header parsing
// =============================================================================

TEST(CartHeader, ParseBasicFields) {
    auto rom = make_rom(0x8000'0400u, {nop()}, "HELLO N64");
    const CartHeader h = parse_cart_header(rom);
    EXPECT_TRUE(h.valid_magic());
    EXPECT_EQ(h.pi_bsd_dom1, 0x8037'1240u);
    EXPECT_EQ(h.pc, 0x8000'0400u);
    EXPECT_EQ(h.entrypoint, 0x8000'0400u); // 6102: no fixup
    EXPECT_EQ(h.cic, CicType::Cic6102);
    EXPECT_EQ(h.cic_seed, 0x3F);
    EXPECT_EQ(h.crc1, 0xDEAD'BEEFu);
    EXPECT_EQ(h.title(), "HELLO N64");
}

TEST(CartHeader, Cic6103EntrypointFixup) {
    EXPECT_EQ(fixup_entrypoint(CicType::Cic6103, 0x8010'0400u), 0x8000'0400u);
    EXPECT_EQ(fixup_entrypoint(CicType::Cic6106, 0x8020'0400u), 0x8000'0400u);
    EXPECT_EQ(fixup_entrypoint(CicType::Cic6102, 0x8000'1000u), 0x8000'1000u);
    EXPECT_EQ(cic_seed(CicType::Cic6105), 0x91);
}

TEST(CartHeader, ShortRomDefaults6102) {
    std::vector<u8> rom(0x80, 0);
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    // entry
    rom[8] = 0x80; rom[9] = 0x00; rom[10] = 0x04; rom[11] = 0x00;
    const CartHeader h = parse_cart_header(rom);
    EXPECT_EQ(h.cic, CicType::Cic6102);
}

// =============================================================================
// HLE boot
// =============================================================================

TEST(BootHle, SetsPcAndCopiesPayload) {
    // Payload: three NOPs + marker word as data after code is fine;
    // use distinct instruction words so we can verify RDRAM contents.
    const u32 entry = 0x8000'0400u;
    std::vector<u32> payload = {
        addiu(8, 0, 0x11), // t0
        addiu(9, 0, 0x22), // t1
        addu(10, 8, 9),    // t2 = 0x33
        nop(),
    };
    auto rom = make_rom(entry, payload);

    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    EXPECT_TRUE(emu.booted());
    EXPECT_EQ(emu.entrypoint(), entry);
    EXPECT_EQ(emu.cpu().pc(), entry);
    EXPECT_EQ(emu.cart_header().cic, CicType::Cic6102);

    // First payload word must sit at physical entry
    const u32 phys = entry & 0x1FFF'FFFFu;
    EXPECT_EQ(emu.bus().read32(phys), payload[0]);
    EXPECT_EQ(emu.bus().read32(phys + 4), payload[1]);
}

TEST(BootHle, ExecutesPayloadWithoutCrash) {
    const u32 entry = 0x8000'0400u;
    // sum-like tiny program, then infinite loop at done
    // t0=1; t1=0; t1+=t0;  (result 1) then j done; nop; done: b done; nop
    const u32 done_off = 5 * 4; // word index 5
    std::vector<u32> payload = {
        addiu(8, 0, 1),          // 0
        addiu(9, 0, 0),          // 1
        addu(9, 9, 8),           // 2  t1 = 1
        j(entry + done_off),     // 3
        nop(),                   // 4 delay
        beq(0, 0, -1),           // 5 done: loop to self
        nop(),                   // 6 delay of branch
    };
    auto rom = make_rom(entry, payload, "BOOT EXEC");

    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    EXPECT_EQ(emu.cpu().pc(), entry);

    // Run enough to finish and land in the loop
    emu.run_cycles(20);

    EXPECT_EQ(emu.cpu().gpr(9), 1u);
    EXPECT_EQ(emu.cpu().exception_count(), 0u);
    // PC should be in the done loop region
    EXPECT_GE(emu.cpu().pc(), entry + done_off);
    EXPECT_LT(emu.cpu().pc(), entry + done_off + 16);
}

TEST(BootHle, SpAndCicSeedInitialized) {
    auto rom = make_rom(0x8000'0400u, {nop()});
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));

    // s6 = CIC seed
    EXPECT_EQ(emu.cpu().gpr(22) & 0xFF, 0x3Fu);
    // sp near top of RDRAM (KSEG1)
    EXPECT_NE(emu.cpu().gpr(29), 0u);
    // PIF RAM control remains idle until software queues Joybus work.
    EXPECT_EQ(emu.pif().ram()[0x3F], 0u);
    EXPECT_FALSE(emu.pif().process_requested());
    // COP0 Status has CU0+CU1
    EXPECT_NE(emu.cpu().cop0(Cop0Reg::Status) & 0x3000'0000u, 0u);
}

TEST(BootHle, PiDom1FromHeader) {
    auto rom = make_rom(0x8000'0400u, {nop()});
    // 0x80371240 → lat=0x40, pwd=0x12, pgs=0x7, rls=0x3?
    // word = 0x80371240
    // lat = 0x40, pwd = 0x12, pgs = 0x7, rls = 0x0 (bits 23:20 of 0x80371240)
    // 0x80371240 >> 20 = 0x803 → rls = 0x3? bits 23:20 = (0x80371240 >> 20) & 0xF
    // 0x80371240 >> 20 = 0x803, & 0xF = 0x3. pgs = (>>16)&0xF = 0x7.
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    EXPECT_EQ(emu.bus().read32(mmio::PI_BASE + PeripheralInterface::BsdDom1Lat), 0x40u);
    EXPECT_EQ(emu.bus().read32(mmio::PI_BASE + PeripheralInterface::BsdDom1Pwd), 0x12u);
    EXPECT_EQ(emu.bus().read32(mmio::PI_BASE + PeripheralInterface::BsdDom1Pgs), 0x7u);
    EXPECT_EQ(emu.bus().read32(mmio::PI_BASE + PeripheralInterface::BsdDom1Rls), 0x3u);
}

TEST(BootHle, SoftResetReboots) {
    const u32 entry = 0x8000'0400u;
    std::vector<u32> payload = {
        addiu(4, 0, 0x55),
        beq(0, 0, -1),
        nop(),
    };
    auto rom = make_rom(entry, payload);
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    emu.run_cycles(5);
    EXPECT_EQ(emu.cpu().gpr(4), 0x55u);

    emu.reset();
    EXPECT_TRUE(emu.booted());
    EXPECT_EQ(emu.cpu().pc(), entry);
    // After reboot, t0 not yet executed
    EXPECT_EQ(emu.cpu().gpr(4), 0u);
    emu.run_cycles(3);
    EXPECT_EQ(emu.cpu().gpr(4), 0x55u);
}

TEST(BootHle, MemoryStoreVisible) {
    // Program writes a word to RDRAM via KSEG0 and loops.
    const u32 entry = 0x8000'0400u;
    const u32 data_va = 0x8000'1000u;
    // lui t0, 0x8000; ori t0, t0, 0x1000; lui t1, 0xA5A5; ori t1, t1, 0xA5A5;
    // sw t1, 0(t0); b .; nop
    std::vector<u32> payload = {
        lui(8, 0x8000),
        ori(8, 8, 0x1000),
        lui(9, 0xA5A5),
        ori(9, 9, 0xA5A5),
        sw(9, 8, 0),
        beq(0, 0, -1),
        nop(),
    };
    auto rom = make_rom(entry, payload);
    // Ensure ROM is large enough that copy covers nothing at 0x1000 phys for data —
    // data region is plain RDRAM.
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    emu.run_cycles(16);
    EXPECT_EQ(emu.cpu().exception_count(), 0u);
    EXPECT_EQ(emu.bus().read32(data_va & 0x1FFF'FFFFu), 0xA5A5'A5A5u);
}

// =============================================================================
// Force CIC override
// =============================================================================

TEST(BootHle, ForceCic6103Fixup) {
    // Raw PC in header as 0xA0100400-style mangled: 0x80100400
    const u32 raw = 0x8010'0400u;
    const u32 fixed = 0x8000'0400u;
    std::vector<u32> payload = {addiu(2, 0, 7), beq(0, 0, -1), nop()};
    auto rom = make_rom(raw, payload);

    BootConfig cfg;
    cfg.force_cic = CicType::Cic6103;
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom, cfg));
    EXPECT_EQ(emu.entrypoint(), fixed);
    EXPECT_EQ(emu.cpu().pc(), fixed);
    EXPECT_EQ(emu.cart_header().cic, CicType::Cic6103);
    emu.run_cycles(8);
    EXPECT_EQ(emu.cpu().gpr(2), 7u);
}

// =============================================================================
// TV / region / seeds (Phase 3 hardening)
// =============================================================================

TEST(CartHeader, TvFromCountry) {
    EXPECT_EQ(tv_from_country('E'), TvType::NTSC);
    EXPECT_EQ(tv_from_country('J'), TvType::NTSC);
    EXPECT_EQ(tv_from_country('P'), TvType::PAL);
    EXPECT_EQ(tv_from_country('D'), TvType::PAL);
    EXPECT_EQ(tv_from_country('B'), TvType::MPAL);
}

TEST(BootHle, TvTypeSeededInS4) {
    auto rom_ntsc = make_rom(0x8000'0400u, {nop()}, "NTSC", 'E');
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom_ntsc));
    EXPECT_EQ(emu.cart_header().tv, TvType::NTSC);
    EXPECT_EQ(emu.cpu().gpr(20), static_cast<u64>(TvType::NTSC)); // s4

    auto rom_pal = make_rom(0x8000'0400u, {nop()}, "PALROM", 'P');
    ASSERT_TRUE(emu.load_rom_bytes(rom_pal));
    EXPECT_EQ(emu.cart_header().tv, TvType::PAL);
    EXPECT_EQ(emu.cpu().gpr(20), static_cast<u64>(TvType::PAL));
}

TEST(BootHle, ForceTvOverride) {
    auto rom = make_rom(0x8000'0400u, {nop()}, "FORCE TV", 'E'); // would be NTSC
    BootConfig cfg;
    cfg.force_tv_set = true;
    cfg.force_tv = TvType::PAL;
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom, cfg));
    EXPECT_EQ(emu.cart_header().tv, TvType::PAL);
    EXPECT_EQ(emu.cpu().gpr(20), static_cast<u64>(TvType::PAL));
}

TEST(BootHle, OsSeedsAndPifSideband) {
    auto rom = make_rom(0x8000'0400u, {nop()});
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    // s3 osRomType = cart (0)
    EXPECT_EQ(emu.cpu().gpr(19), 0u);
    // s5 osResetType = cold (0)
    EXPECT_EQ(emu.cpu().gpr(21), 0u);
    // s6 CIC seed
    EXPECT_EQ(emu.cpu().gpr(22) & 0xFF, 0x3Fu);
    // PIF sideband
    EXPECT_EQ(emu.pif().ram()[0x26], 0x3F);
    EXPECT_EQ(emu.pif().ram()[0x27], 0x3F);
    EXPECT_EQ(emu.pif().ram()[0x3F], 0u);
    EXPECT_FALSE(emu.pif().process_requested());
}

TEST(BootHle, SoftResetSetsResetType) {
    auto rom = make_rom(0x8000'0400u, {nop()});
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    EXPECT_EQ(emu.cpu().gpr(21), 0u); // cold
    emu.reset();
    EXPECT_TRUE(emu.booted());
    EXPECT_EQ(emu.cpu().gpr(21), 1u); // NMI / soft
}

TEST(BootHle, Ipl3MirroredToDmem) {
    auto rom = make_rom(0x8000'0400u, {nop()});
    // Put a marker in the cart IPL3 region
    rom[0x40] = 0xAB;
    rom[0x41] = 0xCD;
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));
    EXPECT_EQ(emu.bus().sp_dmem()[0x40], 0xAB);
    EXPECT_EQ(emu.bus().sp_dmem()[0x41], 0xCD);
    EXPECT_EQ(emu.rsp().dmem()[0x40], 0xAB);
}

TEST(BootHle, UserPifRomLoadAndRead) {
    // Minimal fake PIF ROM: first word = 0x3C08A400 (lui t0, ...) pattern
    std::vector<u8> pif(2048, 0);
    pif[0] = 0x3C; pif[1] = 0x08; pif[2] = 0xA4; pif[3] = 0x00;
    // Write to a temp file
    const std::string path = (std::filesystem::temp_directory_path() / "n64emu_test_pif.bin").string();
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(pif.data()),
                  static_cast<std::streamsize>(pif.size()));
    }

    auto rom = make_rom(0x8000'0400u, {nop()});
    BootConfig cfg;
    cfg.pif_rom_path = path;
    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom, cfg));
    EXPECT_TRUE(emu.bus().has_pif_rom());
    // Physical 0x1FC00000 via KSEG1 0xBFC00000
    EXPECT_EQ(emu.bus().read8(0x1FC0'0000u), 0x3C);
    EXPECT_EQ(emu.bus().read32(0x1FC0'0000u), 0x3C08'A400u);
    // HLE still jumps to cart entry, not PIF
    EXPECT_EQ(emu.cpu().pc(), 0x8000'0400ull);
}

TEST(BootHle, LlePifRequiresRom) {
    Emulator emu;
    BootConfig cfg;
    cfg.mode = BootMode::LlePif;
    // No pif_rom_path → fail
    EXPECT_FALSE(emu.boot(cfg));
}

TEST(BootHle, LlePifStartsAtResetVector) {
    std::vector<u8> pif(2048, 0);
    // NOP at reset vector
    pif[0] = 0; pif[1] = 0; pif[2] = 0; pif[3] = 0;
    const std::string path = (std::filesystem::temp_directory_path() / "n64emu_test_pif_lle.bin").string();
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(pif.data()),
                  static_cast<std::streamsize>(pif.size()));
    }

    BootConfig cfg;
    cfg.mode = BootMode::LlePif;
    cfg.pif_rom_path = path;
    Emulator emu;
    // LLE can boot without cart
    ASSERT_TRUE(emu.boot(cfg));
    EXPECT_EQ(emu.cpu().pc(), 0xBFC0'0000ull);
    // Fetch a NOP from PIF ROM through the CPU bus (KSEG1)
    emu.cpu().step(); // should execute NOP without exception
    EXPECT_EQ(emu.cpu().exception_count(), 0u);
    EXPECT_EQ(emu.cpu().pc(), 0xBFC0'0004ull);
}
