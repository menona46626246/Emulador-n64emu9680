#include <gtest/gtest.h>

#include "n64/ai/ai.hpp"
#include "n64/bus/bus.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/bus/pi.hpp"
#include "n64/bus/si.hpp"
#include "n64/bus/sp_regs.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/cpu/insn.hpp"
#include "n64/pif/pif.hpp"
#include "n64/rcp/rdp/rdp.hpp"
#include "n64/rcp/rsp/rsp.hpp"
#include "n64/vi/vi.hpp"

#include <cstring>
#include <vector>

using namespace n64;
using namespace n64::insn;

namespace {

struct WiredBus {
    Bus bus;
    Cpu cpu;
    VideoInterface vi;
    AudioInterface ai;
    Pif pif;
    Rsp rsp;
    Rdp rdp;

    WiredBus() {
        bus.reset();
        cpu.reset();
        vi.reset();
        ai.reset();
        pif.reset();
        rsp.reset();
        rdp.reset();
        cpu.connect_bus(&bus);
        bus.connect(&cpu, &vi, &ai, &pif, &rsp, &rdp);
    }
};

// KSEG1 uncached physical aliases for MMIO
constexpr u32 kseg1(u32 phys) { return 0xA000'0000u | phys; }

} // namespace

// =============================================================================
// Key address map
// =============================================================================

TEST(MmioMap, MiVersionReadable) {
    WiredBus w;
    EXPECT_EQ(w.bus.read32(mmio::MI_BASE + 4), 0x0202'0102u);
    EXPECT_EQ(w.bus.read32(kseg1(mmio::MI_BASE + 4)), 0x0202'0102u);
}

TEST(MmioMap, ViRegistersRoundTrip) {
    WiredBus w;
    w.bus.write32(mmio::VI_BASE + VideoInterface::Origin, 0x0010'0000u);
    w.bus.write32(mmio::VI_BASE + VideoInterface::Width, 320);
    EXPECT_EQ(w.bus.read32(mmio::VI_BASE + VideoInterface::Origin), 0x0010'0000u);
    EXPECT_EQ(w.bus.read32(mmio::VI_BASE + VideoInterface::Width), 320u);
    EXPECT_EQ(w.vi.origin(), 0x0010'0000u);
    EXPECT_EQ(w.vi.fb_width(), 320);
}

TEST(MmioMap, RiDefaults) {
    WiredBus w;
    EXPECT_EQ(w.bus.read32(mmio::RI_BASE + 0), 0x0Eu); // mode
    EXPECT_EQ(w.bus.read32(mmio::RI_BASE + 4), 0x40u); // config
}

TEST(MmioMap, PiStatusClearIntr) {
    WiredBus w;
    // Force PI interrupt via dummy DMA with empty cart (still finishes)
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::DramAddr, 0x1000);
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::CartAddr, 0x1000'0000);
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::RdLen, 3); // 4 bytes
    EXPECT_NE(w.bus.mi().pending() & mmio::MiIntr::PI, 0u);
    EXPECT_NE(w.bus.read32(mmio::PI_BASE + PeripheralInterface::Status)
                  & PeripheralInterface::StatusIntr, 0u);

    // Clear via PI_STATUS write
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::Status,
                  PeripheralInterface::WrClrIntr);
    EXPECT_EQ(w.bus.mi().pending() & mmio::MiIntr::PI, 0u);
}

// =============================================================================
// PI DMA cart → RDRAM
// =============================================================================

TEST(PiDma, CartToRdram) {
    WiredBus w;
    std::vector<u8> rom(256, 0);
    for (u32 i = 0; i < 64; ++i) {
        rom[i] = static_cast<u8>(0xA0 + i);
    }
    // Keep .z64 magic so endian normalizer leaves data alone for payload area
    rom[0] = 0x80; rom[1] = 0x37; rom[2] = 0x12; rom[3] = 0x40;
    ASSERT_TRUE(w.bus.load_cartridge(rom));

    const u32 dst = 0x2000;
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::DramAddr, dst);
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::CartAddr, 0x1000'0010);
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::RdLen, 15); // 16 bytes

    EXPECT_EQ(w.bus.pi().last_dma_bytes(), 16u);
    for (u32 i = 0; i < 16; ++i) {
        EXPECT_EQ(w.bus.rdram()[dst + i], rom[0x10 + i]) << "byte " << i;
    }
    EXPECT_NE(w.bus.mi().pending() & mmio::MiIntr::PI, 0u);
}

TEST(PiDma, RdramToCart) {
    WiredBus w;
    std::vector<u8> rom(128, 0x00);
    rom[0] = 0x80; rom[1] = 0x37; rom[2] = 0x12; rom[3] = 0x40;
    ASSERT_TRUE(w.bus.load_cartridge(rom));

    for (u32 i = 0; i < 8; ++i) {
        w.bus.rdram()[0x3000 + i] = static_cast<u8>(0x10 + i);
    }

    w.bus.write32(mmio::PI_BASE + PeripheralInterface::DramAddr, 0x3000);
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::CartAddr, 0x1000'0020);
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::WrLen, 7); // 8 bytes

    EXPECT_EQ(w.bus.pi().last_dma_bytes(), 8u);
    for (u32 i = 0; i < 8; ++i) {
        EXPECT_EQ(w.bus.cartridge()[0x20 + i], static_cast<u8>(0x10 + i));
    }
}

// =============================================================================
// SP DMA
// =============================================================================

TEST(SpDma, RdramToDmemAndBack) {
    WiredBus w;
    for (u32 i = 0; i < 32; ++i) {
        w.bus.rdram()[0x4000 + i] = static_cast<u8>(i + 1);
    }

    // MEM_ADDR = 0 (DMEM), DRAM_ADDR = 0x4000, RD_LEN = 31 (32 bytes)
    w.bus.write32(mmio::SP_REGS_BASE + SpRegisters::MemAddr, 0x000);
    w.bus.write32(mmio::SP_REGS_BASE + SpRegisters::DramAddr, 0x4000);
    w.bus.write32(mmio::SP_REGS_BASE + SpRegisters::RdLen, 31);

    EXPECT_EQ(w.bus.sp_regs().last_dma_bytes(), 32u);
    for (u32 i = 0; i < 32; ++i) {
        EXPECT_EQ(w.bus.sp_dmem()[i], static_cast<u8>(i + 1));
    }

    // Clear and DMA back to different RDRAM
    std::fill(w.bus.rdram().begin() + 0x5000, w.bus.rdram().begin() + 0x5000 + 32, 0);
    w.bus.write32(mmio::SP_REGS_BASE + SpRegisters::MemAddr, 0x000);
    w.bus.write32(mmio::SP_REGS_BASE + SpRegisters::DramAddr, 0x5000);
    w.bus.write32(mmio::SP_REGS_BASE + SpRegisters::WrLen, 31);

    for (u32 i = 0; i < 32; ++i) {
        EXPECT_EQ(w.bus.rdram()[0x5000 + i], static_cast<u8>(i + 1));
    }
}

TEST(SpDma, RdramToImem) {
    WiredBus w;
    w.bus.rdram()[0x6000] = 0xDE;
    w.bus.rdram()[0x6001] = 0xAD;
    w.bus.rdram()[0x6002] = 0xBE;
    w.bus.rdram()[0x6003] = 0xEF;

    w.bus.write32(mmio::SP_REGS_BASE + SpRegisters::MemAddr, 0x1000); // IMEM
    w.bus.write32(mmio::SP_REGS_BASE + SpRegisters::DramAddr, 0x6000);
    w.bus.write32(mmio::SP_REGS_BASE + SpRegisters::RdLen, 3);

    EXPECT_EQ(w.bus.sp_imem()[0], 0xDE);
    EXPECT_EQ(w.bus.sp_imem()[1], 0xAD);
    EXPECT_EQ(w.bus.sp_imem()[2], 0xBE);
    EXPECT_EQ(w.bus.sp_imem()[3], 0xEF);
}

TEST(SpRegs, HaltClearStartsRspFlag) {
    WiredBus w;
    EXPECT_TRUE(w.rsp.halted());
    // Clear halt
    w.bus.write32(mmio::SP_REGS_BASE + SpRegisters::Status, SpRegisters::WrClearHalt);
    EXPECT_FALSE(w.rsp.halted());
    EXPECT_EQ(w.bus.read32(mmio::SP_REGS_BASE + SpRegisters::Status) & SpRegisters::StHalt, 0u);

    w.bus.write32(mmio::SP_REGS_BASE + SpRegisters::Status, SpRegisters::WrSetHalt);
    EXPECT_TRUE(w.rsp.halted());
}

// =============================================================================
// SI DMA
// =============================================================================

TEST(SiDma, RoundTripPifRam) {
    WiredBus w;
    for (u32 i = 0; i < 64; ++i) {
        w.bus.rdram()[0x7000 + i] = static_cast<u8>(0xC0 + (i & 0xF));
    }
    // Clear joybus process flag so SI DMA is a pure bulk copy.
    w.bus.rdram()[0x7000 + 0x3F] = 0x00;

    // RDRAM → PIF
    w.bus.write32(mmio::SI_BASE + SerialInterface::DramAddr, 0x7000);
    w.bus.write32(mmio::SI_BASE + SerialInterface::PifAddrWr64b, 0x1FC0'07C0);

    EXPECT_EQ(w.bus.si().last_dma_bytes(), 64u);
    for (u32 i = 0; i < 63; ++i) {
        EXPECT_EQ(w.pif.ram()[i], static_cast<u8>(0xC0 + (i & 0xF)));
    }
    EXPECT_EQ(w.pif.ram()[0x3F], 0x00);
    EXPECT_NE(w.bus.mi().pending() & mmio::MiIntr::SI, 0u);

    // Clear SI intr
    w.bus.write32(mmio::SI_BASE + SerialInterface::Status, 0);
    EXPECT_EQ(w.bus.mi().pending() & mmio::MiIntr::SI, 0u);

    // Mutate PIF and DMA back
    w.pif.ram()[0] = 0x55;
    w.bus.write32(mmio::SI_BASE + SerialInterface::DramAddr, 0x7100);
    w.bus.write32(mmio::SI_BASE + SerialInterface::PifAddrRd64b, 0x1FC0'07C0);
    EXPECT_EQ(w.bus.rdram()[0x7100], 0x55);
}

// =============================================================================
// MI interrupt mask + CPU IP2
// =============================================================================

TEST(MiIntr, MaskAndCpuIp2) {
    WiredBus w;
    // Enable PI in mask: SET_PI is bit 9 (i=4 → set = 1<<(4*2+1)=1<<9)
    w.bus.write32(mmio::MI_BASE + MipsInterface::IntrMask, 1u << 9);
    EXPECT_EQ(w.bus.mi().mask() & mmio::MiIntr::PI, mmio::MiIntr::PI);

    // No pending yet → CPU IP2 low
    EXPECT_EQ(w.cpu.cop0(Cop0Reg::Cause) & mmio::CAUSE_IP2, 0u);

    // Raise PI via DMA
    std::vector<u8> rom(16, 0x11);
    rom[0] = 0x80; rom[1] = 0x37; rom[2] = 0x12; rom[3] = 0x40;
    ASSERT_TRUE(w.bus.load_cartridge(rom));
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::DramAddr, 0);
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::CartAddr, 0x1000'0000);
    w.bus.write32(mmio::PI_BASE + PeripheralInterface::RdLen, 3);

    EXPECT_TRUE(w.bus.mi().cpu_irq_level());
    EXPECT_NE(w.cpu.cop0(Cop0Reg::Cause) & mmio::CAUSE_IP2, 0u);
}

TEST(MiIntr, CpuTakesInterrupt) {
    WiredBus w;
    // Status: IE=1, IM2 (bit 10) enabled for IP2, BEV=0, EXL=0
    // IM bits are Status[15:8]; IP2 = bit 10 → enable bit 10 in Status.
    w.cpu.set_cop0(Cop0Reg::Status, 0x0000'0401u); // IE | IM2
    w.cpu.set_pc(0x8000'1000ull);
    // NOP landing pad + handler at 0x80000180
    for (u32 i = 0; i < 16; ++i) {
        w.bus.write32(0x1000 + i * 4, 0); // NOPs at PC
    }
    // Handler: addiu t0, zero, 0x99 ; eret-less spin (just set marker)
    w.bus.write32(0x180, addiu(8, 0, 0x99));
    w.bus.write32(0x184, 0); // nop

    // Enable VI mask and raise VI
    w.bus.write32(mmio::MI_BASE + MipsInterface::IntrMask, 1u << 7); // SET_VI
    w.vi.raise_interrupt();

    EXPECT_NE(w.cpu.cop0(Cop0Reg::Cause) & mmio::CAUSE_IP2, 0u);

    const u64 before = w.cpu.exception_count();
    w.cpu.step(); // should take interrupt before/at fetch
    EXPECT_EQ(w.cpu.exception_count(), before + 1);
    EXPECT_EQ((w.cpu.cop0(Cop0Reg::Cause) >> 2) & 0x1F, ExcCode::Int);
    // General exception vector (BEV=0): 0x80000180
    EXPECT_EQ(w.cpu.pc(), 0x8000'0180ull);
}

TEST(ViIntr, ClearOnWriteCurrent) {
    WiredBus w;
    w.bus.write32(mmio::MI_BASE + MipsInterface::IntrMask, 1u << 7); // SET_VI
    w.vi.raise_interrupt();
    EXPECT_NE(w.bus.mi().pending() & mmio::MiIntr::VI, 0u);

    w.bus.write32(mmio::VI_BASE + VideoInterface::VCurrent, 0);
    EXPECT_EQ(w.bus.mi().pending() & mmio::MiIntr::VI, 0u);
}

TEST(AiIntr, CompletesAndRaises) {
    WiredBus w;
    w.bus.write32(mmio::MI_BASE + MipsInterface::IntrMask, 1u << 5); // SET_AI
    // Put a few silent stereo frames in RDRAM.
    for (u32 i = 0; i < 0x100; ++i) {
        w.bus.rdram()[0x1000 + i] = 0;
    }
    w.bus.write32(mmio::AI_BASE + AudioInterface::DramAddr, 0x1000);
    w.bus.write32(mmio::AI_BASE + AudioInterface::Len, 0x100);
    // DMA is timed now — force completion for this smoke test.
    w.ai.complete_dma();
    EXPECT_NE(w.bus.mi().pending() & mmio::MiIntr::AI, 0u);

    w.bus.write32(mmio::AI_BASE + AudioInterface::Status, 0);
    EXPECT_EQ(w.bus.mi().pending() & mmio::MiIntr::AI, 0u);
}

// =============================================================================
// CPU program driving PI DMA via MMIO (KSEG1)
// =============================================================================

TEST(BusIntegration, CpuProgramPiDma) {
    WiredBus w;
    std::vector<u8> rom(64, 0);
    rom[0] = 0x80; rom[1] = 0x37; rom[2] = 0x12; rom[3] = 0x40;
    for (int i = 16; i < 32; ++i) rom[static_cast<std::size_t>(i)] = static_cast<u8>(i);
    ASSERT_TRUE(w.bus.load_cartridge(rom));

    // Program at 0x80001000:
    //   lui   t0, 0xA460        ; PI base KSEG1
    //   lui   t1, 0x0000
    //   ori   t1, t1, 0x2000    ; dram addr
    //   sw    t1, 0(t0)         ; PI_DRAM_ADDR
    //   lui   t1, 0x1000
    //   ori   t1, t1, 0x0010    ; cart 0x10000010
    //   sw    t1, 4(t0)         ; PI_CART_ADDR
    //   addiu t1, zero, 15
    //   sw    t1, 8(t0)         ; PI_RD_LEN → kick DMA
    //   nop
    const u32 base = 0x1000;
    const u32 virt = 0x8000'1000;
    std::vector<u32> code = {
        lui(8, 0xA460),
        lui(9, 0x0000),
        ori(9, 9, 0x2000),
        sw(9, 8, 0),
        lui(9, 0x1000),
        ori(9, 9, 0x0010),
        sw(9, 8, 4),
        addiu(9, 0, 15),
        sw(9, 8, 8),
        nop(),
    };
    for (std::size_t i = 0; i < code.size(); ++i) {
        w.bus.write32(base + static_cast<u32>(i) * 4, code[i]);
    }

    w.cpu.set_cop0(Cop0Reg::Status, 0x2400'0000u);
    w.cpu.set_pc(virt);
    w.cpu.run(static_cast<Cycles>(code.size()));

    EXPECT_EQ(w.bus.pi().last_dma_bytes(), 16u);
    for (u32 i = 0; i < 16; ++i) {
        EXPECT_EQ(w.bus.rdram()[0x2000 + i], rom[0x10 + i]);
    }
}

// =============================================================================
// Emulator wiring smoke
// =============================================================================

TEST(EmulatorBus, ComponentsWired) {
    Emulator emu;
    EXPECT_EQ(emu.bus().read32(mmio::MI_BASE + 4), 0x0202'0102u);
    emu.vi().raise_interrupt();
    // Without mask, pending is set but cpu_irq_level is false
    EXPECT_NE(emu.bus().mi().pending() & mmio::MiIntr::VI, 0u);
    EXPECT_FALSE(emu.bus().mi().cpu_irq_level());

    emu.bus().write32(mmio::MI_BASE + MipsInterface::IntrMask, 1u << 7);
    EXPECT_TRUE(emu.bus().mi().cpu_irq_level());
}
