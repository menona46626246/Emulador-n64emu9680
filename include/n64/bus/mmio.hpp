#pragma once

#include "n64/common/types.hpp"

namespace n64::mmio {

// Physical base addresses (29-bit physical map).
// Reference: publicly documented N64 memory map (n64brew / homebrew docs).

// RDRAM
inline constexpr u32 RDRAM_BASE      = 0x0000'0000;
inline constexpr u32 RDRAM_END       = 0x03EF'FFFF;

// RDRAM registers (RI side / RDRAM config) — rarely used by homebrew
inline constexpr u32 RDRAM_REGS_BASE = 0x03F0'0000;
inline constexpr u32 RDRAM_REGS_END  = 0x03FF'FFFF;

// SP (RSP) memory + registers
inline constexpr u32 SP_DMEM_BASE    = 0x0400'0000;
inline constexpr u32 SP_IMEM_BASE    = 0x0400'1000;
inline constexpr u32 SP_REGS_BASE    = 0x0404'0000;
inline constexpr u32 SP_REGS2_BASE   = 0x0408'0000; // SP_PC etc.

// DP (RDP) command / span registers
inline constexpr u32 DP_CMD_BASE     = 0x0410'0000;
inline constexpr u32 DP_SPAN_BASE    = 0x0420'0000;

// MI (MIPS Interface)
inline constexpr u32 MI_BASE         = 0x0430'0000;

// VI (Video Interface)
inline constexpr u32 VI_BASE         = 0x0440'0000;

// AI (Audio Interface)
inline constexpr u32 AI_BASE         = 0x0450'0000;

// PI (Peripheral Interface)
inline constexpr u32 PI_BASE         = 0x0460'0000;

// RI (RDRAM Interface)
inline constexpr u32 RI_BASE         = 0x0470'0000;

// SI (Serial Interface)
inline constexpr u32 SI_BASE         = 0x0480'0000;

// Cartridge Domain 1 Address 2 (ROM)
inline constexpr u32 CART_DOM1_BASE  = 0x1000'0000;
inline constexpr u32 CART_DOM1_END   = 0x1FBF'FFFF;

// PIF / boot ROM region
inline constexpr u32 PIF_ROM_BASE    = 0x1FC0'0000;
inline constexpr u32 PIF_RAM_BASE    = 0x1FC0'07C0;
inline constexpr u32 PIF_RAM_END     = 0x1FC0'07FF;

// MI interrupt bits (MI_INTR / MI_INTR_MASK)
namespace MiIntr {
    inline constexpr u32 SP = 1u << 0;
    inline constexpr u32 SI = 1u << 1;
    inline constexpr u32 AI = 1u << 2;
    inline constexpr u32 VI = 1u << 3;
    inline constexpr u32 PI = 1u << 4;
    inline constexpr u32 DP = 1u << 5;
}

// COP0 Cause IP bit for RCP (typically IP2 = bit 10)
inline constexpr u32 CAUSE_IP2 = 1u << 10;

[[nodiscard]] inline constexpr bool in_range(u32 addr, u32 base, u32 size) noexcept {
    return addr >= base && addr < base + size;
}

} // namespace n64::mmio
