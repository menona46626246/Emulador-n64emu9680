#pragma once

/// Helpers to encode MIPS III instructions for unit tests and synthetic programs.

#include "n64/common/types.hpp"

namespace n64::insn {

// Primary opcodes
inline constexpr u32 SPECIAL = 0x00;
inline constexpr u32 REGIMM  = 0x01;
inline constexpr u32 J       = 0x02;
inline constexpr u32 JAL     = 0x03;
inline constexpr u32 BEQ     = 0x04;
inline constexpr u32 BNE     = 0x05;
inline constexpr u32 BLEZ    = 0x06;
inline constexpr u32 BGTZ    = 0x07;
inline constexpr u32 ADDI    = 0x08;
inline constexpr u32 ADDIU   = 0x09;
inline constexpr u32 SLTI    = 0x0A;
inline constexpr u32 SLTIU   = 0x0B;
inline constexpr u32 ANDI    = 0x0C;
inline constexpr u32 ORI     = 0x0D;
inline constexpr u32 XORI    = 0x0E;
inline constexpr u32 LUI     = 0x0F;
inline constexpr u32 COP0    = 0x10;
inline constexpr u32 BEQL    = 0x14;
inline constexpr u32 BNEL    = 0x15;
inline constexpr u32 DADDIU  = 0x19;
inline constexpr u32 LB      = 0x20;
inline constexpr u32 LH      = 0x21;
inline constexpr u32 LW      = 0x23;
inline constexpr u32 LBU     = 0x24;
inline constexpr u32 LHU     = 0x25;
inline constexpr u32 LWU     = 0x27;
inline constexpr u32 SB      = 0x28;
inline constexpr u32 SH      = 0x29;
inline constexpr u32 SW      = 0x2B;
inline constexpr u32 LL      = 0x30;
inline constexpr u32 LLD     = 0x34;
inline constexpr u32 LD      = 0x37;
inline constexpr u32 SC      = 0x38;
inline constexpr u32 SCD     = 0x3C;
inline constexpr u32 SD      = 0x3F;

// SPECIAL functions
inline constexpr u32 FN_SLL  = 0x00;
inline constexpr u32 FN_SRL  = 0x02;
inline constexpr u32 FN_SRA  = 0x03;
inline constexpr u32 FN_SLLV = 0x04;
inline constexpr u32 FN_SRLV = 0x06;
inline constexpr u32 FN_SRAV = 0x07;
inline constexpr u32 FN_JR   = 0x08;
inline constexpr u32 FN_JALR = 0x09;
inline constexpr u32 FN_SYSCALL = 0x0C;
inline constexpr u32 FN_BREAK   = 0x0D;
inline constexpr u32 FN_MFHI = 0x10;
inline constexpr u32 FN_MTHI = 0x11;
inline constexpr u32 FN_MFLO = 0x12;
inline constexpr u32 FN_MTLO = 0x13;
inline constexpr u32 FN_MULT = 0x18;
inline constexpr u32 FN_MULTU= 0x19;
inline constexpr u32 FN_DIV  = 0x1A;
inline constexpr u32 FN_DIVU = 0x1B;
inline constexpr u32 FN_DMULT= 0x1C;
inline constexpr u32 FN_ADD  = 0x20;
inline constexpr u32 FN_ADDU = 0x21;
inline constexpr u32 FN_SUB  = 0x22;
inline constexpr u32 FN_SUBU = 0x23;
inline constexpr u32 FN_AND  = 0x24;
inline constexpr u32 FN_OR   = 0x25;
inline constexpr u32 FN_XOR  = 0x26;
inline constexpr u32 FN_NOR  = 0x27;
inline constexpr u32 FN_SLT  = 0x2A;
inline constexpr u32 FN_SLTU = 0x2B;
inline constexpr u32 FN_DADD = 0x2C;
inline constexpr u32 FN_DSUB = 0x2E;
inline constexpr u32 FN_DSLL = 0x38;
inline constexpr u32 FN_DSLL32 = 0x3C;

// REGIMM
inline constexpr u32 RT_BLTZ   = 0x00;
inline constexpr u32 RT_BGEZ   = 0x01;
inline constexpr u32 RT_BLTZAL = 0x10;
inline constexpr u32 RT_BGEZAL = 0x11;

[[nodiscard]] inline constexpr u32 rtype(u32 fn, u32 rd, u32 rs, u32 rt, u32 sa = 0) noexcept {
    return (SPECIAL << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | fn;
}

[[nodiscard]] inline constexpr u32 itype(u32 op, u32 rt, u32 rs, u16 imm) noexcept {
    return (op << 26) | (rs << 21) | (rt << 16) | imm;
}

[[nodiscard]] inline constexpr u32 itype_s(u32 op, u32 rt, u32 rs, s16 imm) noexcept {
    return itype(op, rt, rs, static_cast<u16>(imm));
}

[[nodiscard]] inline constexpr u32 jtype(u32 op, u32 target) noexcept {
    return (op << 26) | (target & 0x03FFFFFF);
}

[[nodiscard]] inline constexpr u32 regimm(u32 rt_code, u32 rs, s16 imm) noexcept {
    return (REGIMM << 26) | (rs << 21) | (rt_code << 16) | static_cast<u16>(imm);
}

// Convenience encoders
[[nodiscard]] inline constexpr u32 nop() noexcept { return 0; }
[[nodiscard]] inline constexpr u32 sll(u32 rd, u32 rt, u32 sa) noexcept { return rtype(FN_SLL, rd, 0, rt, sa); }
[[nodiscard]] inline constexpr u32 srl(u32 rd, u32 rt, u32 sa) noexcept { return rtype(FN_SRL, rd, 0, rt, sa); }
[[nodiscard]] inline constexpr u32 sra(u32 rd, u32 rt, u32 sa) noexcept { return rtype(FN_SRA, rd, 0, rt, sa); }
[[nodiscard]] inline constexpr u32 sllv(u32 rd, u32 rt, u32 rs) noexcept { return rtype(FN_SLLV, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 jr(u32 rs) noexcept { return rtype(FN_JR, 0, rs, 0); }
[[nodiscard]] inline constexpr u32 jalr(u32 rd, u32 rs) noexcept { return rtype(FN_JALR, rd, rs, 0); }
[[nodiscard]] inline constexpr u32 syscall() noexcept { return rtype(FN_SYSCALL, 0, 0, 0); }
[[nodiscard]] inline constexpr u32 brk(u32 code = 0) noexcept {
    return rtype(FN_BREAK, 0, 0, 0) | ((code & 0xFFFFF) << 6);
}
[[nodiscard]] inline constexpr u32 mfhi(u32 rd) noexcept { return rtype(FN_MFHI, rd, 0, 0); }
[[nodiscard]] inline constexpr u32 mflo(u32 rd) noexcept { return rtype(FN_MFLO, rd, 0, 0); }
[[nodiscard]] inline constexpr u32 mult(u32 rs, u32 rt) noexcept { return rtype(FN_MULT, 0, rs, rt); }
[[nodiscard]] inline constexpr u32 multu(u32 rs, u32 rt) noexcept { return rtype(FN_MULTU, 0, rs, rt); }
[[nodiscard]] inline constexpr u32 div_(u32 rs, u32 rt) noexcept { return rtype(FN_DIV, 0, rs, rt); }
[[nodiscard]] inline constexpr u32 divu(u32 rs, u32 rt) noexcept { return rtype(FN_DIVU, 0, rs, rt); }
[[nodiscard]] inline constexpr u32 dmult(u32 rs, u32 rt) noexcept { return rtype(FN_DMULT, 0, rs, rt); }
[[nodiscard]] inline constexpr u32 add(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_ADD, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 addu(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_ADDU, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 sub(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_SUB, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 subu(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_SUBU, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 dadd(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_DADD, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 dsub(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_DSUB, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 and_(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_AND, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 or_(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_OR, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 xor_(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_XOR, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 nor(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_NOR, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 slt(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_SLT, rd, rs, rt); }
[[nodiscard]] inline constexpr u32 sltu(u32 rd, u32 rs, u32 rt) noexcept { return rtype(FN_SLTU, rd, rs, rt); }

[[nodiscard]] inline constexpr u32 addiu(u32 rt, u32 rs, s16 imm) noexcept { return itype_s(ADDIU, rt, rs, imm); }
[[nodiscard]] inline constexpr u32 addi(u32 rt, u32 rs, s16 imm) noexcept { return itype_s(ADDI, rt, rs, imm); }
[[nodiscard]] inline constexpr u32 slti(u32 rt, u32 rs, s16 imm) noexcept { return itype_s(SLTI, rt, rs, imm); }
[[nodiscard]] inline constexpr u32 sltiu(u32 rt, u32 rs, s16 imm) noexcept { return itype_s(SLTIU, rt, rs, imm); }
[[nodiscard]] inline constexpr u32 andi(u32 rt, u32 rs, u16 imm) noexcept { return itype(ANDI, rt, rs, imm); }
[[nodiscard]] inline constexpr u32 ori(u32 rt, u32 rs, u16 imm) noexcept { return itype(ORI, rt, rs, imm); }
[[nodiscard]] inline constexpr u32 xori(u32 rt, u32 rs, u16 imm) noexcept { return itype(XORI, rt, rs, imm); }
[[nodiscard]] inline constexpr u32 lui(u32 rt, u16 imm) noexcept { return itype(LUI, rt, 0, imm); }

[[nodiscard]] inline constexpr u32 lw(u32 rt, u32 rs, s16 off) noexcept { return itype_s(LW, rt, rs, off); }
[[nodiscard]] inline constexpr u32 lh(u32 rt, u32 rs, s16 off) noexcept { return itype_s(LH, rt, rs, off); }
[[nodiscard]] inline constexpr u32 lb(u32 rt, u32 rs, s16 off) noexcept { return itype_s(LB, rt, rs, off); }
[[nodiscard]] inline constexpr u32 lbu(u32 rt, u32 rs, s16 off) noexcept { return itype_s(LBU, rt, rs, off); }
[[nodiscard]] inline constexpr u32 lhu(u32 rt, u32 rs, s16 off) noexcept { return itype_s(LHU, rt, rs, off); }
[[nodiscard]] inline constexpr u32 sw(u32 rt, u32 rs, s16 off) noexcept { return itype_s(SW, rt, rs, off); }
[[nodiscard]] inline constexpr u32 sh(u32 rt, u32 rs, s16 off) noexcept { return itype_s(SH, rt, rs, off); }
[[nodiscard]] inline constexpr u32 sb(u32 rt, u32 rs, s16 off) noexcept { return itype_s(SB, rt, rs, off); }
[[nodiscard]] inline constexpr u32 ld(u32 rt, u32 rs, s16 off) noexcept { return itype_s(LD, rt, rs, off); }
[[nodiscard]] inline constexpr u32 sd(u32 rt, u32 rs, s16 off) noexcept { return itype_s(SD, rt, rs, off); }
[[nodiscard]] inline constexpr u32 ll(u32 rt, u32 rs, s16 off) noexcept { return itype_s(LL, rt, rs, off); }
[[nodiscard]] inline constexpr u32 lld(u32 rt, u32 rs, s16 off) noexcept { return itype_s(LLD, rt, rs, off); }
[[nodiscard]] inline constexpr u32 sc(u32 rt, u32 rs, s16 off) noexcept { return itype_s(SC, rt, rs, off); }
[[nodiscard]] inline constexpr u32 scd(u32 rt, u32 rs, s16 off) noexcept { return itype_s(SCD, rt, rs, off); }

[[nodiscard]] inline constexpr u32 beq(u32 rs, u32 rt, s16 off) noexcept { return itype_s(BEQ, rt, rs, off); }
[[nodiscard]] inline constexpr u32 bne(u32 rs, u32 rt, s16 off) noexcept { return itype_s(BNE, rt, rs, off); }
[[nodiscard]] inline constexpr u32 blez(u32 rs, s16 off) noexcept { return itype_s(BLEZ, 0, rs, off); }
[[nodiscard]] inline constexpr u32 bgtz(u32 rs, s16 off) noexcept { return itype_s(BGTZ, 0, rs, off); }
[[nodiscard]] inline constexpr u32 beql(u32 rs, u32 rt, s16 off) noexcept { return itype_s(BEQL, rt, rs, off); }
[[nodiscard]] inline constexpr u32 bnel(u32 rs, u32 rt, s16 off) noexcept { return itype_s(BNEL, rt, rs, off); }

[[nodiscard]] inline constexpr u32 j(u32 addr) noexcept { return jtype(J, addr >> 2); }
[[nodiscard]] inline constexpr u32 jal(u32 addr) noexcept { return jtype(JAL, addr >> 2); }

[[nodiscard]] inline constexpr u32 bltz(u32 rs, s16 off) noexcept { return regimm(RT_BLTZ, rs, off); }
[[nodiscard]] inline constexpr u32 bgez(u32 rs, s16 off) noexcept { return regimm(RT_BGEZ, rs, off); }
[[nodiscard]] inline constexpr u32 bgezal(u32 rs, s16 off) noexcept { return regimm(RT_BGEZAL, rs, off); }

[[nodiscard]] inline constexpr u32 mfc0(u32 rt, u32 rd) noexcept {
    return (COP0 << 26) | (0u << 21) | (rt << 16) | (rd << 11);
}
[[nodiscard]] inline constexpr u32 mtc0(u32 rt, u32 rd) noexcept {
    return (COP0 << 26) | (4u << 21) | (rt << 16) | (rd << 11);
}
[[nodiscard]] inline constexpr u32 eret() noexcept {
    return (COP0 << 26) | (1u << 25) | 0x18;
}
[[nodiscard]] inline constexpr u32 tlbr() noexcept {
    return (COP0 << 26) | (0x10u << 21) | 0x01u;
}
[[nodiscard]] inline constexpr u32 tlbwi() noexcept {
    return (COP0 << 26) | (0x10u << 21) | 0x02u;
}
[[nodiscard]] inline constexpr u32 tlbwr() noexcept {
    return (COP0 << 26) | (0x10u << 21) | 0x06u;
}
[[nodiscard]] inline constexpr u32 tlbp() noexcept {
    return (COP0 << 26) | (0x10u << 21) | 0x08u;
}

// Pseudo: li rt, imm32 via lui+ori (returns only single-insn forms separately)
[[nodiscard]] inline constexpr u32 dsll(u32 rd, u32 rt, u32 sa) noexcept {
    return rtype(FN_DSLL, rd, 0, rt, sa);
}
[[nodiscard]] inline constexpr u32 dsll32(u32 rd, u32 rt, u32 sa) noexcept {
    return rtype(FN_DSLL32, rd, 0, rt, sa);
}

} // namespace n64::insn
