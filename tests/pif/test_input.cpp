#include <gtest/gtest.h>

#include "n64/bus/bus.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/bus/si.hpp"
#include "n64/core/emulator.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/cpu/insn.hpp"
#include "n64/pif/pif.hpp"

#include <cstring>
#include <vector>

using namespace n64;
using namespace n64::insn;

namespace {

/// Build a standard 64-byte PIF command block for controller channel 0 status.
/// Layout (libultra-style):
///   [0]=01 tlen  [1]=04 rlen  [2]=01 cmd  [3..6]=rx  [7]=FE term  [0x3F]=01 process
void write_controller_status_cmd(std::span<u8> ram, bool set_process = true) {
    std::fill(ram.begin(), ram.end(), 0);
    ram[0] = 0x01; // tx len = 1 (command byte)
    ram[1] = 0x04; // rx len = 4
    ram[2] = JoybusCmd::Controller;
    ram[3] = ram[4] = ram[5] = ram[6] = 0x00; // rx placeholder
    ram[7] = 0xFE; // terminator
    if (set_process) {
        ram[0x3F] = Pif::kCtrlProcess;
    }
}

void write_info_cmd(std::span<u8> ram) {
    std::fill(ram.begin(), ram.end(), 0);
    ram[0] = 0x01;
    ram[1] = 0x03;
    ram[2] = JoybusCmd::Info;
    ram[3] = ram[4] = ram[5] = 0;
    ram[6] = 0xFE;
    ram[0x3F] = Pif::kCtrlProcess;
}

} // namespace

// =============================================================================
// Direct joybus
// =============================================================================

TEST(Joybus, ControllerStatusButtonsAndStick) {
    Pif pif;
    pif.reset();

    ControllerState st;
    st.present = true;
    st.buttons = static_cast<u16>(Button::A) | static_cast<u16>(Button::Start) |
                 static_cast<u16>(Button::DUp);
    st.stick_x = 40;
    st.stick_y = -20;
    pif.set_controller(0, st);

    write_controller_status_cmd(pif.ram());
    EXPECT_TRUE(pif.process_requested());
    EXPECT_GE(pif.process_joybus(), 1);
    EXPECT_FALSE(pif.process_requested()); // flag cleared

    // Response at bytes 3..6
    const auto ram = pif.ram();
    const u16 buttons = (static_cast<u16>(ram[3]) << 8) | ram[4];
    EXPECT_EQ(buttons, st.buttons);
    EXPECT_EQ(static_cast<s8>(ram[5]), 40);
    EXPECT_EQ(static_cast<s8>(ram[6]), static_cast<s8>(-20));
}

TEST(Joybus, InfoReturnsControllerType) {
    Pif pif;
    pif.reset();
    write_info_cmd(pif.ram());
    pif.process_joybus();
    const auto ram = pif.ram();
    const u16 type = static_cast<u16>(ram[3]) | (static_cast<u16>(ram[4]) << 8);
    EXPECT_EQ(type, kControllerType);
    EXPECT_EQ(ram[5], kControllerStatus);
}

TEST(Joybus, AbsentControllerSetsErrorBit) {
    Pif pif;
    pif.reset();
    ControllerState st;
    st.present = false;
    pif.set_controller(0, st);

    write_controller_status_cmd(pif.ram());
    pif.process_joybus();
    // rlen byte should have bit7 set
    EXPECT_NE(pif.ram()[1] & 0x80u, 0u);
}

TEST(Joybus, MultiChannelSkipEmpty) {
    Pif pif;
    pif.reset();
    // ch0 skip (00 00), ch1 controller
    auto ram = pif.ram();
    std::fill(ram.begin(), ram.end(), 0);
    ram[0] = 0x00;
    ram[1] = 0x00; // skip ch0
    ram[2] = 0x01;
    ram[3] = 0x04;
    ram[4] = JoybusCmd::Controller;
    // rx at 5..8
    ram[9] = 0xFE;
    ram[0x3F] = Pif::kCtrlProcess;

    ControllerState st;
    st.present = true;
    st.buttons = static_cast<u16>(Button::B);
    // Put pad on channel 1
    pif.set_controller(0, ControllerState{0, 0, 0, false});
    pif.set_controller(1, st);

    pif.process_joybus();
    const u16 buttons = (static_cast<u16>(ram[5]) << 8) | ram[6];
    EXPECT_EQ(buttons, static_cast<u16>(Button::B));
}

// =============================================================================
// SI DMA path
// =============================================================================

TEST(JoybusSi, DmaRoundTripFillsResponse) {
    Emulator emu;
    // Prepare command block in RDRAM
    constexpr u32 kCmdAddr = 0x0000'8000u;
    std::array<u8, 64> cmd{};
    write_controller_status_cmd(cmd);

    ControllerState st;
    st.buttons = static_cast<u16>(Button::A) | static_cast<u16>(Button::B);
    st.stick_x = 10;
    st.stick_y = 20;
    emu.pif().set_controller(0, st);

    auto rdram = emu.bus().rdram();
    std::copy(cmd.begin(), cmd.end(), rdram.begin() + kCmdAddr);

    // SI write: DRAM_ADDR then WR64 (RDRAM→PIF) then RD64 (PIF→RDRAM)
    emu.bus().write32(mmio::SI_BASE + SerialInterface::DramAddr, kCmdAddr);
    emu.bus().write32(mmio::SI_BASE + SerialInterface::PifAddrWr64b, 0x1FC0'07C0);

    // After WR, joybus should have run; responses live in PIF RAM.
    {
        const auto pr = emu.pif().ram();
        const u16 buttons = (static_cast<u16>(pr[3]) << 8) | pr[4];
        EXPECT_EQ(buttons, st.buttons);
        EXPECT_EQ(static_cast<s8>(pr[5]), 10);
        EXPECT_EQ(static_cast<s8>(pr[6]), 20);
    }

    // RD back to a different RDRAM buffer
    constexpr u32 kRespAddr = 0x0000'9000u;
    emu.bus().write32(mmio::SI_BASE + SerialInterface::DramAddr, kRespAddr);
    emu.bus().write32(mmio::SI_BASE + SerialInterface::PifAddrRd64b, 0x1FC0'07C0);

    const u16 buttons = (static_cast<u16>(rdram[kRespAddr + 3]) << 8) |
                        rdram[kRespAddr + 4];
    EXPECT_EQ(buttons, st.buttons);
    EXPECT_NE(emu.bus().mi().pending() & mmio::MiIntr::SI, 0u);
}

TEST(JoybusSi, MmioControlByteTriggersProcess) {
    Emulator emu;
    write_controller_status_cmd(emu.pif().ram(), /*set_process=*/false);
    // Write commands into PIF via bus window without process flag
    for (std::size_t i = 0; i < 8; ++i) {
        emu.bus().write8(static_cast<u32>(mmio::PIF_RAM_BASE + i), emu.pif().ram()[i]);
    }
    ControllerState st;
    st.buttons = static_cast<u16>(Button::Start);
    emu.pif().set_controller(0, st);

    // Kick process via control byte
    emu.bus().write8(mmio::PIF_RAM_BASE + 0x3F, Pif::kCtrlProcess);

    const auto ram = emu.pif().ram();
    const u16 buttons = (static_cast<u16>(ram[3]) << 8) | ram[4];
    EXPECT_EQ(buttons, static_cast<u16>(Button::Start));
    EXPECT_EQ(ram[0x3F] & Pif::kCtrlProcess, 0); // cleared
}

// =============================================================================
// CPU program polls controller via SI
// =============================================================================

TEST(JoybusIntegration, CpuPollsController) {
    // Program:
    //  1. Build command block at 0x80008000 in RDRAM (already pre-filled)
    //  2. SI DRAM_ADDR = 0x8000
    //  3. SI WR64
    //  4. SI DRAM_ADDR = 0x9000
    //  5. SI RD64
    //  6. LHU t0, 0x9003  — actually buttons at +3/+4; load word at 0x9000 and shift
    //  7. loop
    const u32 entry = 0x8000'0400u;

    // Prebuild ROM with code; data block filled after boot into RDRAM.
    std::vector<u32> code = {
        // t0 = SI base 0xA4800000
        lui(8, 0xA480),
        // DRAM_ADDR = 0x8000
        lui(9, 0x0000),
        ori(9, 9, 0x8000),
        sw(9, 8, SerialInterface::DramAddr),
        // WR64 kick (any value)
        sw(0, 8, SerialInterface::PifAddrWr64b),
        // DRAM_ADDR = 0x9000
        ori(9, 0, 0x9000),
        sw(9, 8, SerialInterface::DramAddr),
        // RD64
        sw(0, 8, SerialInterface::PifAddrRd64b),
        // t1 = 0x80009000; lw t2, 0(t1) → bytes 0..3; buttons are at +3,+4
        // Simpler: lbu t2, 3(t1); lbu t3, 4(t1); combine
        lui(9, 0x8000),
        ori(9, 9, 0x9000),
        lbu(10, 9, 3), // buttons hi
        lbu(11, 9, 4), // buttons lo
        sll(10, 10, 8),
        or_(10, 10, 11), // t2 = buttons
        // store buttons to 0x8000A000 for easy check
        lui(9, 0x8000),
        ori(9, 9, 0xA000),
        sw(10, 9, 0),
        beq(0, 0, -1),
        nop(),
    };

    // Build minimal rom
    std::vector<u8> rom(0x1000 + code.size() * 4, 0);
    auto put32 = [&](std::size_t off, u32 v) {
        rom[off] = static_cast<u8>(v >> 24);
        rom[off + 1] = static_cast<u8>(v >> 16);
        rom[off + 2] = static_cast<u8>(v >> 8);
        rom[off + 3] = static_cast<u8>(v);
    };
    put32(0x00, 0x80371240);
    put32(0x08, entry);
    for (std::size_t i = 0; i < code.size(); ++i) {
        put32(0x1000 + i * 4, code[i]);
    }

    Emulator emu;
    ASSERT_TRUE(emu.load_rom_bytes(rom));

    // Prefill command block at phys 0x8000
    std::array<u8, 64> cmd{};
    write_controller_status_cmd(cmd);
    std::copy(cmd.begin(), cmd.end(), emu.bus().rdram().begin() + 0x8000);

    ControllerState st;
    st.buttons = static_cast<u16>(Button::A) | static_cast<u16>(Button::Z);
    st.stick_x = 0;
    st.stick_y = 0;
    emu.pif().set_controller(0, st);

    emu.run_cycles(80);

    const u32 got = emu.bus().read32(0xA000);
    EXPECT_EQ(got, st.buttons);
    EXPECT_EQ(emu.cpu().exception_count(), 0u);
}

// =============================================================================
// Button helpers
// =============================================================================

TEST(ControllerState, SetButton) {
    ControllerState s;
    s.set_button(Button::A, true);
    s.set_button(Button::B, true);
    EXPECT_TRUE(s.button(Button::A));
    EXPECT_TRUE(s.button(Button::B));
    s.set_button(Button::A, false);
    EXPECT_FALSE(s.button(Button::A));
    EXPECT_TRUE(s.button(Button::B));
}
