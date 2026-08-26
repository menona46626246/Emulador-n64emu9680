#include <gtest/gtest.h>

#include "n64/core/emulator.hpp"
#include "n64/cpu/block_cache.hpp"
#include "n64/cpu/insn.hpp"
#include "tests/cpu/test_cpu_harness.hpp"

using namespace n64;
using namespace n64::insn;
using n64::test::CpuHarness;

namespace {

constexpr u32 entry_lo(u32 paddr, bool valid = true, bool dirty = true,
                       bool global = false) {
    return ((paddr >> 12) << 6) |
           (dirty ? 0x4u : 0u) |
           (valid ? 0x2u : 0u) |
           (global ? 0x1u : 0u);
}

void install_tlb(CpuHarness& h, u32 index, u32 vaddr, u8 asid,
                 u32 even_paddr, u32 odd_paddr, u32 page_mask = 0,
                 bool even_valid = true, bool even_dirty = true,
                 bool odd_valid = true, bool odd_dirty = true,
                 bool global = false) {
    h.cpu().set_cop0(Cop0Reg::Index, index);
    h.cpu().set_cop0(Cop0Reg::PageMask, page_mask);
    h.cpu().set_cop0(Cop0Reg::EntryHi, (vaddr & 0xFFFF'E000u) | asid);
    h.cpu().set_cop0(Cop0Reg::EntryLo0,
                     entry_lo(even_paddr, even_valid, even_dirty, global));
    h.cpu().set_cop0(Cop0Reg::EntryLo1,
                     entry_lo(odd_paddr, odd_valid, odd_dirty, global));
    h.write_code({tlbwi()});
    h.start();
    h.run_steps(1);
}

u32 exception_code(const Cpu& cpu) {
    return (cpu.cop0(Cop0Reg::Cause) >> 2) & 0x1Fu;
}

} // namespace

TEST(CpuTlb, ResetHasNoKusegIdentityMapping) {
    Cpu cpu;
    cpu.reset();
    PhysicalAddress paddr = 0;

    EXPECT_TRUE(cpu.translate(0x8000'1234u, false, paddr));
    EXPECT_EQ(paddr, 0x0000'1234u);
    EXPECT_TRUE(cpu.translate(0xA000'5678u, false, paddr));
    EXPECT_EQ(paddr, 0x0000'5678u);
    EXPECT_FALSE(cpu.translate(0x0000'1000u, false, paddr));
    EXPECT_EQ(cpu.cop0(Cop0Reg::Random), 31u);
}

TEST(CpuTlb, TlbwiMapsEvenAndOddPages) {
    CpuHarness h;
    constexpr u32 kVirt = 0x0040'0000u;
    install_tlb(h, 3, kVirt, 0x12, 0x0001'0000u, 0x0001'2000u);

    PhysicalAddress paddr = 0;
    EXPECT_TRUE(h.cpu().translate(kVirt + 0x234u, false, paddr));
    EXPECT_EQ(paddr, 0x0001'0234u);
    EXPECT_TRUE(h.cpu().translate(kVirt + 0x1000u + 0x345u, true, paddr));
    EXPECT_EQ(paddr, 0x0001'2345u);
}

TEST(CpuTlb, MappedLoadAndStoreReachPhysicalMemory) {
    CpuHarness h;
    constexpr u32 kVirt = 0x0060'0000u;
    constexpr u32 kPhys = 0x0002'0000u;
    install_tlb(h, 2, kVirt, 7, kPhys, kPhys + 0x1000u);
    h.poke32(kPhys, 0x1234'5678u);
    h.cpu().set_gpr(4, kVirt);
    h.cpu().set_gpr(6, 0xCAFE'BABEu);
    h.write_code({lw(5, 4, 0), sw(6, 4, 4)});
    h.start();
    h.run_steps(2);

    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(5)), 0x1234'5678u);
    EXPECT_EQ(h.peek32(kPhys + 4), 0xCAFE'BABEu);
}

TEST(CpuTlb, InstructionFetchUsesMappedPhysicalPage) {
    CpuHarness h;
    constexpr u32 kVirt = 0x0070'0000u;
    constexpr u32 kPhys = 0x0002'8000u;
    install_tlb(h, 5, kVirt, 7, kPhys, kPhys + 0x1000u);
    h.write_code({addiu(8, 0, 42)}, kPhys);

    h.start(kVirt);
    h.run_steps(1);

    EXPECT_EQ(h.cpu().gpr(8), 42u);
    EXPECT_EQ(h.cpu().pc(), kVirt + 4u);
}

TEST(CpuTlb, AsidIsolationAndGlobalMapping) {
    CpuHarness h;
    constexpr u32 kVirt = 0x0080'0000u;
    install_tlb(h, 4, kVirt, 0x21, 0x0003'0000u, 0x0003'1000u);

    PhysicalAddress paddr = 0;
    h.cpu().set_cop0(Cop0Reg::EntryHi, kVirt | 0x22u);
    EXPECT_FALSE(h.cpu().translate(kVirt, false, paddr));

    install_tlb(h, 4, kVirt, 0x21, 0x0003'0000u, 0x0003'1000u,
                0, true, true, true, true, true);
    h.cpu().set_cop0(Cop0Reg::EntryHi, kVirt | 0x7Fu);
    EXPECT_TRUE(h.cpu().translate(kVirt + 0x80u, false, paddr));
    EXPECT_EQ(paddr, 0x0003'0080u);
}

TEST(CpuTlb, PageMaskSelectsLargeEvenAndOddPages) {
    CpuHarness h;
    constexpr u32 kVirt = 0x0100'0000u;
    constexpr u32 kMask16KiB = 0x0000'6000u;
    install_tlb(h, 6, kVirt, 3, 0x0004'0000u, 0x0004'4000u,
                kMask16KiB);

    PhysicalAddress paddr = 0;
    EXPECT_TRUE(h.cpu().translate(kVirt + 0x3FFCu, false, paddr));
    EXPECT_EQ(paddr, 0x0004'3FFCu);
    EXPECT_TRUE(h.cpu().translate(kVirt + 0x4000u + 0x123u, false, paddr));
    EXPECT_EQ(paddr, 0x0004'4123u);
}

TEST(CpuTlb, CleanPageRaisesModificationExceptionOnStore) {
    CpuHarness h;
    constexpr u32 kVirt = 0x0120'0000u;
    constexpr u32 kPhys = 0x0005'0000u;
    install_tlb(h, 7, kVirt, 5, kPhys, kPhys + 0x1000u,
                0, true, false);
    h.poke32(kPhys, 0xAAAA'5555u);
    h.cpu().set_gpr(4, kVirt);
    h.cpu().set_gpr(5, 0xDEAD'BEEFu);
    h.write_code({sw(5, 4, 0)});
    h.start();
    h.run_steps(1);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::Mod);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::BadVAddr), kVirt);
    EXPECT_EQ(h.cpu().pc(), 0x8000'0180u);
    EXPECT_EQ(h.peek32(kPhys), 0xAAAA'5555u);
}

TEST(CpuTlb, InvalidEntryUsesGeneralVector) {
    CpuHarness h;
    constexpr u32 kVirt = 0x0140'0000u;
    install_tlb(h, 8, kVirt, 9, 0x0006'0000u, 0x0006'1000u,
                0, false, false);
    h.cpu().set_gpr(4, kVirt);
    h.write_code({lw(5, 4, 0)});
    h.start();
    h.run_steps(1);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::TLBL);
    EXPECT_EQ(h.cpu().pc(), 0x8000'0180u);
}

TEST(CpuTlb, MissUsesRefillVectorAndUpdatesCop0State) {
    CpuHarness h;
    constexpr u32 kVirt = 0x0160'3450u;
    h.cpu().set_cop0(Cop0Reg::EntryHi, 0x2Au);
    h.cpu().set_cop0(Cop0Reg::Context, 0xAB80'0000u);
    h.cpu().set_gpr(4, kVirt);
    h.write_code({lw(5, 4, 0)});
    h.start();
    h.run_steps(1);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::TLBL);
    EXPECT_EQ(h.cpu().pc(), 0x8000'0000u);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::BadVAddr), kVirt);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::EntryHi),
              (kVirt & 0xFFFF'E000u) | 0x2Au);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::Context),
              0xAB80'0000u | ((kVirt >> 9) & 0x007F'FFF0u));
}

TEST(CpuTlb, TlbpAndTlbrExposeIndexedEntry) {
    CpuHarness h;
    constexpr u32 kVirt = 0x0180'0000u;
    constexpr u32 kEvenLo = entry_lo(0x0007'0000u, true, true);
    constexpr u32 kOddLo = entry_lo(0x0007'4000u, true, false);
    install_tlb(h, 11, kVirt, 0x33, 0x0007'0000u, 0x0007'4000u,
                0x0000'6000u, true, true, true, false);

    h.cpu().set_cop0(Cop0Reg::EntryHi, kVirt | 0x33u);
    h.write_code({tlbp()});
    h.start();
    h.run_steps(1);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::Index), 11u);

    h.cpu().set_cop0(Cop0Reg::PageMask, 0);
    h.cpu().set_cop0(Cop0Reg::EntryHi, 0);
    h.cpu().set_cop0(Cop0Reg::EntryLo0, 0);
    h.cpu().set_cop0(Cop0Reg::EntryLo1, 0);
    h.cpu().set_cop0(Cop0Reg::Index, 11);
    h.write_code({tlbr()});
    h.start();
    h.run_steps(1);

    EXPECT_EQ(h.cpu().cop0(Cop0Reg::PageMask), 0x0000'6000u);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::EntryHi), kVirt | 0x33u);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::EntryLo0), kEvenLo);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::EntryLo1), kOddLo);
}

TEST(CpuTlb, TlbpMissSetsProbeFailureBit) {
    CpuHarness h;
    h.cpu().set_cop0(Cop0Reg::EntryHi, 0x0200'0044u);
    h.write_code({tlbp()});
    h.start();
    h.run_steps(1);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::Index), 0x8000'0000u);
}

TEST(CpuTlb, TlbwrUsesRandomEntryAboveWired) {
    CpuHarness h;
    constexpr u32 kVirt = 0x0220'0000u;
    h.cpu().set_cop0(Cop0Reg::Wired, 30);
    h.cpu().set_cop0(Cop0Reg::PageMask, 0);
    h.cpu().set_cop0(Cop0Reg::EntryHi, kVirt | 1u);
    h.cpu().set_cop0(Cop0Reg::EntryLo0, entry_lo(0x0008'0000u));
    h.cpu().set_cop0(Cop0Reg::EntryLo1, entry_lo(0x0008'1000u));
    h.write_code({tlbwr()});
    h.start();
    h.run_steps(1);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::Random), 30u);

    h.cpu().set_cop0(Cop0Reg::Index, 31);
    h.write_code({tlbr()});
    h.start();
    h.run_steps(1);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::EntryHi), kVirt | 1u);
}

TEST(CpuTlb, UserModeCannotFetchKseg0) {
    CpuHarness h;
    h.cpu().set_cop0(Cop0Reg::Status, 0x2000'0010u); // CU0 + KSU=user
    h.start();
    h.run_steps(1);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::AdEL);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::BadVAddr), CpuHarness::kBaseVirt);
    EXPECT_EQ(h.cpu().pc(), 0x8000'0180u);
}

TEST(CpuTlb, MappingChangeInvalidatesCachedVirtualCode) {
    CpuHarness h;
    BlockCache cache;
    h.cpu().set_block_cache(&cache);
    constexpr u32 kVirt = 0x0240'0000u;
    constexpr u32 kPhysA = 0x0009'0000u;
    constexpr u32 kPhysB = 0x0009'2000u;
    install_tlb(h, 12, kVirt, 6, kPhysA, kPhysA + 0x1000u);
    h.write_code({addiu(8, 0, 1)}, kPhysA);
    h.write_code({addiu(8, 0, 2)}, kPhysB);

    ASSERT_NE(cache.compile(kVirt, h.bus(), h.cpu()), nullptr);
    ASSERT_NE(cache.lookup(kVirt), nullptr);

    h.cpu().set_cop0(Cop0Reg::Index, 12);
    h.cpu().set_cop0(Cop0Reg::EntryLo0, entry_lo(kPhysB));
    h.cpu().set_cop0(Cop0Reg::EntryLo1, entry_lo(kPhysB + 0x1000u));
    h.write_code({tlbwi()});
    h.start();
    h.run_steps(1);

    EXPECT_EQ(cache.lookup(kVirt), nullptr);
    h.cpu().set_gpr(8, 0);
    h.start(kVirt);
    h.run_steps(1);
    EXPECT_EQ(h.cpu().gpr(8), 2u);
}

TEST(CpuTlb, PhysicalWriteInvalidatesCachedTlbCode) {
    Emulator emu;
    Cpu& cpu = emu.cpu();
    Bus& bus = emu.bus();
    BlockCache* cache = cpu.block_cache();
    ASSERT_NE(cache, nullptr);
    constexpr u32 kVirt = 0x0250'0000u;
    constexpr u32 kPhys = 0x000A'0000u;

    cpu.set_cop0(Cop0Reg::Status, 0x2400'0000u);
    cpu.set_cop0(Cop0Reg::Index, 13);
    cpu.set_cop0(Cop0Reg::PageMask, 0);
    cpu.set_cop0(Cop0Reg::EntryHi, kVirt | 8u);
    cpu.set_cop0(Cop0Reg::EntryLo0, entry_lo(kPhys));
    cpu.set_cop0(Cop0Reg::EntryLo1, entry_lo(kPhys + 0x1000u));
    bus.write32(CpuHarness::kBasePhys, tlbwi());
    cpu.set_pc(CpuHarness::kBaseVirt);
    cpu.run(1);

    bus.write32(kPhys, addiu(8, 0, 1));
    ASSERT_NE(cache->compile(kVirt, bus, cpu), nullptr);
    ASSERT_NE(cache->lookup(kVirt), nullptr);

    bus.write32(kPhys, addiu(8, 0, 2));
    EXPECT_EQ(cache->lookup(kVirt), nullptr);
    cpu.set_gpr(8, 0);
    cpu.set_pc(kVirt);
    cpu.run(1);
    EXPECT_EQ(cpu.gpr(8), 2u);
}

TEST(CpuTlb, NestedExceptionPreservesOriginalEpc) {
    CpuHarness h;
    constexpr u32 kMissingVirt = 0x0260'0000u;
    h.cpu().set_gpr(4, kMissingVirt);
    h.write_code({lw(5, 4, 0)});
    h.start();
    h.run_steps(1);
    const u32 first_epc = h.cpu().cop0(Cop0Reg::EPC);

    h.cpu().set_pc(0x8000'1234u);
    h.cpu().raise_exception(ExcCode::Bp);

    EXPECT_EQ(h.cpu().cop0(Cop0Reg::EPC), first_epc);
    EXPECT_EQ(exception_code(h.cpu()), ExcCode::Bp);
    EXPECT_EQ(h.cpu().pc(), 0x8000'0180u);
}
