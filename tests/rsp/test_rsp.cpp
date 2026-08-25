#include <gtest/gtest.h>

#include "n64/bus/bus.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/bus/sp_regs.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/rcp/rdp/commands.hpp"
#include "n64/rcp/rsp/insn.hpp"
#include "n64/rcp/rsp/rsp.hpp"

#include <vector>

using namespace n64;
using namespace n64::rsp_insn;

namespace {

void load_imem(Rsp& rsp, std::initializer_list<u32> words, u32 base = 0) {
    auto im = rsp.imem();
    u32 off = base;
    for (u32 w : words) {
        store_be32_sp(im, off, w);
        off += 4;
    }
}

} // namespace

// =============================================================================
// Scalar ALU / control
// =============================================================================

TEST(RspScalar, AddiuAdduAndBreak) {
    Emulator emu;
    auto& rsp = emu.rsp();
    rsp.reset();

    // t0=1; t1=2; t2=t0+t1; break
    load_imem(rsp, {
        addiu(8, 0, 1),
        addiu(9, 0, 2),
        addu(10, 8, 9),
        brk(),
        nop(),
    });
    rsp.set_pc(0);
    rsp.set_halted(false);
    rsp.set_intr_on_break(true);

    const u32 steps = rsp.run(16);
    EXPECT_GE(steps, 4u);
    EXPECT_TRUE(rsp.halted());
    EXPECT_TRUE(rsp.broke());
    EXPECT_EQ(rsp.gpr(10), 3u);
    EXPECT_NE(emu.bus().mi().pending() & mmio::MiIntr::SP, 0u);
}

TEST(RspScalar, BranchDelaySlot) {
    Emulator emu;
    auto& rsp = emu.rsp();
    rsp.reset();
    // beq zero,zero, +2 ; addiu t0,0,1 (delay) ; addiu t1,0,2 (skip) ; addiu t2,0,3 ; break
    load_imem(rsp, {
        beq(0, 0, 2),
        addiu(8, 0, 1),
        addiu(9, 0, 2),
        addiu(10, 0, 3),
        brk(),
    });
    rsp.set_pc(0);
    rsp.set_halted(false);
    rsp.run(16);
    EXPECT_EQ(rsp.gpr(8), 1u);
    EXPECT_EQ(rsp.gpr(9), 0u);
    EXPECT_EQ(rsp.gpr(10), 3u);
}

TEST(RspScalar, DmemLoadStore) {
    Emulator emu;
    auto& rsp = emu.rsp();
    rsp.reset();
    // sw pattern to DMEM 0x10, lw back
    load_imem(rsp, {
        lui(8, 0xDEAD),
        ori(8, 8, 0xBEEF),
        addiu(9, 0, 0x10),
        sw(8, 9, 0),
        lw(10, 9, 0),
        brk(),
    });
    rsp.set_pc(0);
    rsp.set_halted(false);
    rsp.run(16);
    EXPECT_EQ(rsp.gpr(10), 0xDEADBEEFu);
    EXPECT_EQ(rsp.dmem()[0x10], 0xDE);
    EXPECT_EQ(rsp.dmem()[0x13], 0xEF);
}

TEST(RspScalar, DmemStoresAreImmediatelyVisibleOnBus) {
    Emulator emu;
    auto& rsp = emu.rsp();
    rsp.reset();
    load_imem(rsp, {
        lui(8, 0xDEAD),
        ori(8, 8, 0xBEEF),
        addiu(9, 0, 0x20),
        sw(8, 9, 0),
        nop(),
    });
    rsp.set_pc(0);
    rsp.set_halted(false);
    ASSERT_EQ(rsp.run(4), 4u);
    EXPECT_FALSE(rsp.halted());
    EXPECT_EQ(emu.bus().read32(0x0400'0020u), 0xDEAD'BEEFu);
}

TEST(RspRdpIntegration, Cop0DpcRunsFreshXbusCommandList) {
    Emulator emu;
    auto& rsp = emu.rsp();
    rsp.reset();

    constexpr u64 command = rdp_cmd::sync_full();
    constexpr u32 command_hi = static_cast<u32>(command >> 32);
    constexpr u32 command_lo = static_cast<u32>(command);
    load_imem(rsp, {
        lui(8, static_cast<u16>(command_hi >> 16)),
        ori(8, 8, static_cast<u16>(command_hi)),
        sw(8, 0, 0),
        lui(8, static_cast<u16>(command_lo >> 16)),
        ori(8, 8, static_cast<u16>(command_lo)),
        sw(8, 0, 4),
        addiu(9, 0, 2), // DPC_STATUS set XBUS source
        mtc0(9, 11),
        mtc0(0, 8),     // DPC_START = 0
        addiu(9, 0, 8),
        mtc0(9, 9),     // DPC_END = 8, executes immediately
        brk(),
    });
    rsp.set_pc(0);
    rsp.set_halted(false);
    const u64 before = emu.rdp().commands_executed();
    rsp.run(32);

    EXPECT_EQ(emu.bus().read32(0x0400'0000u), command_hi);
    EXPECT_EQ(emu.bus().read32(0x0400'0004u), command_lo);
    EXPECT_EQ(emu.rdp().commands_executed(), before + 1);
    EXPECT_NE(emu.bus().mi().pending() & mmio::MiIntr::DP, 0u);
}

TEST(RspScalar, JAndJr) {
    Emulator emu;
    auto& rsp = emu.rsp();
    rsp.reset();
    // 0: j 0x10; 1: addiu t0,0,1; ... 4(0x10): addiu t1,0,2; break
    load_imem(rsp, {
        j(0x10),
        addiu(8, 0, 1),   // delay
        addiu(9, 0, 9),   // skip
        nop(),
        addiu(10, 0, 2),  // @0x10
        brk(),
    });
    rsp.set_pc(0);
    rsp.set_halted(false);
    rsp.run(16);
    EXPECT_EQ(rsp.gpr(8), 1u);
    EXPECT_EQ(rsp.gpr(9), 0u);
    EXPECT_EQ(rsp.gpr(10), 2u);
}

// =============================================================================
// DMA + task from CPU side
// =============================================================================

TEST(RspTask, CpuDmaImemAndRun) {
    Emulator emu;
    auto& bus = emu.bus();

    // Build a tiny program in RDRAM, DMA to IMEM, clear halt, wait for broke.
    // Program: addiu t0,0,0x42; addiu t1,0,0x20; sw t0,0(t1); break
    const u32 prog_phys = 0x3000;
    const std::vector<u32> prog = {
        addiu(8, 0, 0x42),
        addiu(9, 0, 0x20),
        sw(8, 9, 0),
        brk(),
    };
    auto rdram = bus.rdram();
    for (std::size_t i = 0; i < prog.size(); ++i) {
        store_be32(rdram, prog_phys + static_cast<u32>(i) * 4, prog[i]);
    }

    // SP_MEM_ADDR = IMEM 0x1000, DRAM_ADDR = prog, RD_LEN = 16-1
    bus.write32(mmio::SP_REGS_BASE + SpRegisters::MemAddr, 0x1000);
    bus.write32(mmio::SP_REGS_BASE + SpRegisters::DramAddr, prog_phys);
    bus.write32(mmio::SP_REGS_BASE + SpRegisters::RdLen, 15);

    EXPECT_EQ(bus.sp_regs().last_dma_bytes(), 16u);

    // PC = 0, clear halt + set INTR_ON_BREAK
    bus.write32(mmio::SP_REGS2_BASE + SpRegisters::Pc, 0);
    bus.write32(mmio::SP_REGS_BASE + SpRegisters::Status,
                SpRegisters::WrClearHalt | SpRegisters::WrClearBroke |
                SpRegisters::WrSetIntrBreak);

    EXPECT_FALSE(emu.rsp().halted());

    // Let emulator step CPU+RSP
    for (int i = 0; i < 50 && !emu.rsp().broke(); ++i) {
        emu.run_cycles(8);
    }

    EXPECT_TRUE(emu.rsp().broke());
    EXPECT_TRUE(emu.rsp().halted());
    // Result in DMEM
    emu.rsp().push_mem_to_bus();
    EXPECT_EQ(bus.sp_dmem()[0x20], 0x00);
    EXPECT_EQ(bus.sp_dmem()[0x23], 0x42);
    EXPECT_NE(bus.mi().pending() & mmio::MiIntr::SP, 0u);
}

TEST(RspTask, DmaRoundTripDoesNotBlockCpu) {
    Emulator emu;
    // Place NOPs in RDRAM for CPU
    for (u32 i = 0; i < 64; ++i) {
        emu.bus().write32(i * 4, 0);
    }
    emu.cpu().set_pc(0x8000'0000ull);
    emu.cpu().set_cop0(Cop0Reg::Status, 0x2400'0000u);

    // Start a long-ish RSP loop that eventually breaks
    // t0=100; loop: addiu t0,t0,-1; bne t0,0,loop; nop; break
    load_imem(emu.rsp(), {
        addiu(8, 0, 100),
        addiu(8, 8, -1),
        bne(8, 0, -2),
        nop(),
        brk(),
    });
    emu.rsp().set_pc(0);
    emu.rsp().set_halted(false);

    const u64 cpu_before = emu.cpu().cycles();
    emu.run_cycles(200);
    const u64 cpu_after = emu.cpu().cycles();
    EXPECT_EQ(cpu_after - cpu_before, 200u); // CPU kept advancing
    // RSP should finish within this budget
    EXPECT_TRUE(emu.rsp().broke() || emu.rsp().gpr(8) < 100u);
}

TEST(RspTask, HaltStopsExecution) {
    Emulator emu;
    auto& rsp = emu.rsp();
    load_imem(rsp, {
        addiu(8, 0, 1),
        addiu(8, 8, 1),
        beq(0, 0, -2), // infinite
        nop(),
    });
    rsp.set_pc(0);
    rsp.set_halted(false);
    rsp.run(10);
    EXPECT_GE(rsp.gpr(8), 2u);
    rsp.set_halted(true);
    const u32 snap = rsp.gpr(8);
    rsp.run(50);
    EXPECT_EQ(rsp.gpr(8), snap);
}

// =============================================================================
// SP status / PC MMIO
// =============================================================================

TEST(RspMmio, StatusAndPc) {
    Emulator emu;
    EXPECT_NE(emu.bus().read32(mmio::SP_REGS_BASE + SpRegisters::Status) &
                  SpRegisters::StHalt, 0u);

    emu.bus().write32(mmio::SP_REGS2_BASE + SpRegisters::Pc, 0x40);
    EXPECT_EQ(emu.rsp().pc(), 0x40u);
    EXPECT_EQ(emu.bus().read32(mmio::SP_REGS2_BASE + SpRegisters::Pc), 0x40u);

    emu.bus().write32(mmio::SP_REGS_BASE + SpRegisters::Status,
                      SpRegisters::WrClearHalt);
    EXPECT_FALSE(emu.rsp().halted());
    emu.bus().write32(mmio::SP_REGS_BASE + SpRegisters::Status,
                      SpRegisters::WrSetHalt);
    EXPECT_TRUE(emu.rsp().halted());
}

TEST(RspProgram, SumInDmem) {
    // Sum bytes at DMEM 0..3 into word at 0x10, then break.
    Emulator emu;
    auto& rsp = emu.rsp();
    rsp.dmem()[0] = 1;
    rsp.dmem()[1] = 2;
    rsp.dmem()[2] = 3;
    rsp.dmem()[3] = 4;
    // t0=0 (sum); t1=0 (ptr); t2=4 (end)
    // loop: lbu t3,0(t1); addu t0,t0,t3; addiu t1,1; bne t1,t2,loop; nop
    // sw t0, 0x10(zero); break
    load_imem(rsp, {
        addiu(8, 0, 0),
        addiu(9, 0, 0),
        addiu(10, 0, 4),
        lbu(11, 9, 0),       // loop
        addu(8, 8, 11),
        addiu(9, 9, 1),
        bne(9, 10, -4),
        nop(),
        addiu(12, 0, 0x10),
        sw(8, 12, 0),
        brk(),
    });
    rsp.set_pc(0);
    rsp.set_halted(false);
    rsp.run(64);
    EXPECT_TRUE(rsp.broke());
    EXPECT_EQ(rsp.gpr(8), 10u);
    EXPECT_EQ(rsp.dmem()[0x10], 0);
    EXPECT_EQ(rsp.dmem()[0x13], 10);
}
