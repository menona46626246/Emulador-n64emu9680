#pragma once

/// Encode RSP scalar MIPS-I subset instructions (same layout as VR4300 32-bit).

#include "n64/common/types.hpp"

#include <span>

namespace n64::rsp_insn {

inline constexpr u32 SPECIAL = 0x00;
inline constexpr u32 REGIMM  = 0x01;
inline constexpr u32 J       = 0x02;
inline constexpr u32 JAL     = 0x03;
inline constexpr u32 BEQ     = 0x04;
inline constexpr u32 BNE     = 0x05;
inline constexpr u32 ADDIU   = 0x09;
inline constexpr u32 ANDI    = 0x0C;
inline constexpr u32 ORI     = 0x0D;
inline constexpr u32 LUI     = 0x0F;
inline constexpr u32 COP0    = 0x10;
inline constexpr u32 LW      = 0x23;
inline constexpr u32 SW      = 0x2B;
inline constexpr u32 LB      = 0x20;
inline constexpr u32 LBU     = 0x24;
inline constexpr u32 SB      = 0x28;

inline constexpr u32 FN_SLL  = 0x00;
inline constexpr u32 FN_SRL  = 0x02;
inline constexpr u32 FN_JR   = 0x08;
inline constexpr u32 FN_JALR = 0x09;
inline constexpr u32 FN_BREAK= 0x0D;
inline constexpr u32 FN_ADDU = 0x21;
inline constexpr u32 FN_SUBU = 0x23;
inline constexpr u32 FN_AND  = 0x24;
inline constexpr u32 FN_OR   = 0x25;
inline constexpr u32 FN_XOR  = 0x26;
inline constexpr u32 FN_SLT  = 0x2A;

[[nodiscard]] inline constexpr u32 rtype(u32 fn, u32 rd, u32 rs, u32 rt, u32 sa = 0) noexcept {
    return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | fn;
}
[[nodiscard]] inline constexpr u32 itype(u32 op, u32 rt, u32 rs, u16 imm) noexcept {
    return (op << 26) | (rs << 21) | (rt << 16) | imm;
}
[[nodiscard]] inline constexpr u32 itype_s(u32 op, u32 rt, u32 rs, s16 imm) noexcept {
    return itype(op, rt, rs, static_cast<u16>(imm));
}
[[nodiscard]] inline constexpr u32 jtype(u32 op, u32 targ) noexcept {
    return (op << 26) | (targ & 0x03FFFFFF);
}

[[nodiscard]] inline constexpr u32 nop() noexcept { return 0; }
[[nodiscard]] inline constexpr u32 sll(u32 rd, u32 rt, u32 sa) noexcept { return rtype(FN_SLL, rd, 0, rt, sa); }
[[nodiscard]] inline constexpr u32 srl(u32 rd, u32 rt, u32 sa) noexcept { return rtype(FN_SRL, rd, 0, rt, sa); }
[[nodiscard]] inline constexpr u32 jr(u32 rs) noexcept { return rtype(FN_JR, 0, rs, 0); }
[[nodiscard]] inline constexpr u32 jalr(u32 rd, u32 rs) noexcept { return rtype(FN_JALR, rd, rs, 0); }
[[nodiscard]] inline constexpr u32 brk(u32 code = 0) noexcept {
    return rtype(FN_BREAK, 0, 0, 0) | ((code & 0xFFFFF) << 6);
}
[[nodiscard]] inline constexpr u32 addu(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_ADDU, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 subu(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_SUBU, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 and_(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_AND, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 or_(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_OR, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 xor_(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_XOR, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 slt(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_SLT, rd, rs, rt); }

[[nodiscard]] inline constexpr u32 addiu(u32 rt, u32 rs, s16 imm) noexcept { return itype_s(ADDIU, rt, rs, imm); }
[[nodiscard]] inline constexpr u32 andi(u32 rt, u32 rs, u16 imm) noexcept { return itype(ANDI, rt, rs, imm); }
[[nodiscard]] inline constexpr u32 ori(u32 rt, u32 rs, u16 imm) noexcept { return itype(ORI, rt, rs, imm); }
[[nodiscard]] inline constexpr u32 lui(u32 rt, u16 imm) noexcept { return itype(LUI, rt, 0, imm); }
[[nodiscard]] inline constexpr u32 lw(u32 rt, u32 rs, s16 off) noexcept { return itype_s(LW, rt, rs, off); }
[[nodiscard]] inline constexpr u32 sw(u32 rt, u32 rs, s16 off) noexcept { return itype_s(SW, rt, rs, off); }
[[nodiscard]] inline constexpr u32 lb(u32 rt, u32 rs, s16 off) noexcept { return itype_s(LB, rt, rs, off); }
[[nodiscard]] inline constexpr u32 lbu(u32 rt, u32 rs, s16 off) noexcept { return itype_s(LBU, rt, rs, off); }
[[nodiscard]] inline constexpr u32 sb(u32 rt, u32 rs, s16 off) noexcept { return itype_s(SB, rt, rs, off); }
[[nodiscard]] inline constexpr u32 beq(u32 rs, u32 rt, s16 off) noexcept { return itype_s(BEQ, rt, rs, off); }
[[nodiscard]] inline constexpr u32 bne(u32 rs, u32 rt, s16 off) noexcept { return itype_s(BNE, rt, rs, off); }
[[nodiscard]] inline constexpr u32 j(u32 addr) noexcept { return jtype(J, (addr >> 2) & 0x3FF); }
[[nodiscard]] inline constexpr u32 jal(u32 addr) noexcept { return jtype(JAL, (addr >> 2) & 0x3FF); }

[[nodiscard]] inline constexpr u32 mfc0(u32 rt, u32 rd) noexcept {
    return (COP0 << 26) | (0u << 21) | (rt << 16) | (rd << 11);
}
[[nodiscard]] inline constexpr u32 mtc0(u32 rt, u32 rd) noexcept {
    return (COP0 << 26) | (4u << 21) | (rt << 16) | (rd << 11);
}

/// Store BE word into a byte buffer (IMEM/DMEM/RDRAM). No address mask —
/// caller supplies a correctly bounded offset.
inline void store_be32(std::span<u8> mem, u32 off, u32 w) {
    if (off + 3 >= mem.size()) {
        return;
    }
    mem[off]     = static_cast<u8>((w >> 24) & 0xFF);
    mem[off + 1] = static_cast<u8>((w >> 16) & 0xFF);
    mem[off + 2] = static_cast<u8>((w >> 8) & 0xFF);
    mem[off + 3] = static_cast<u8>(w & 0xFF);
}

/// Store BE word into RSP IMEM/DMEM (12-bit wrap).
inline void store_be32_sp(std::span<u8> mem, u32 off, u32 w) {
    store_be32(mem, off & 0xFFFu, w);
}

} // namespace n64::rsp_insn
