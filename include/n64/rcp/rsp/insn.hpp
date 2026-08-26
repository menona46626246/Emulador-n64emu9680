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
inline constexpr u32 COP2    = 0x12;
inline constexpr u32 LW      = 0x23;
inline constexpr u32 SW      = 0x2B;
inline constexpr u32 LB      = 0x20;
inline constexpr u32 LBU     = 0x24;
inline constexpr u32 SB      = 0x28;
inline constexpr u32 LWC2    = 0x32;
inline constexpr u32 SWC2    = 0x3A;

inline constexpr u32 COP2_MFC = 0x00;
inline constexpr u32 COP2_CFC = 0x02;
inline constexpr u32 COP2_MTC = 0x04;
inline constexpr u32 COP2_CTC = 0x06;

inline constexpr u32 VF_VMULF = 0x00;
inline constexpr u32 VF_VMULU = 0x01;
inline constexpr u32 VF_VRNDP = 0x02;
inline constexpr u32 VF_VMULQ = 0x03;
inline constexpr u32 VF_VMUDL = 0x04;
inline constexpr u32 VF_VMUDM = 0x05;
inline constexpr u32 VF_VMUDN = 0x06;
inline constexpr u32 VF_VMUDH = 0x07;
inline constexpr u32 VF_VMACF = 0x08;
inline constexpr u32 VF_VMACU = 0x09;
inline constexpr u32 VF_VRNDN = 0x0A;
inline constexpr u32 VF_VMACQ = 0x0B;
inline constexpr u32 VF_VMADL = 0x0C;
inline constexpr u32 VF_VMADM = 0x0D;
inline constexpr u32 VF_VMADN = 0x0E;
inline constexpr u32 VF_VMADH = 0x0F;
inline constexpr u32 VF_VADD  = 0x10;
inline constexpr u32 VF_VSUB  = 0x11;
inline constexpr u32 VF_VABS  = 0x13;
inline constexpr u32 VF_VADDC = 0x14;
inline constexpr u32 VF_VSUBC = 0x15;
inline constexpr u32 VF_VSAR  = 0x1D;
inline constexpr u32 VF_VLT   = 0x20;
inline constexpr u32 VF_VEQ   = 0x21;
inline constexpr u32 VF_VNE   = 0x22;
inline constexpr u32 VF_VGE   = 0x23;
inline constexpr u32 VF_VCL   = 0x24;
inline constexpr u32 VF_VCH   = 0x25;
inline constexpr u32 VF_VCR   = 0x26;
inline constexpr u32 VF_VMRG  = 0x27;
inline constexpr u32 VF_VAND  = 0x28;
inline constexpr u32 VF_VNAND = 0x29;
inline constexpr u32 VF_VOR   = 0x2A;
inline constexpr u32 VF_VNOR  = 0x2B;
inline constexpr u32 VF_VXOR  = 0x2C;
inline constexpr u32 VF_VNXOR = 0x2D;
inline constexpr u32 VF_VRCP  = 0x30;
inline constexpr u32 VF_VRCPL = 0x31;
inline constexpr u32 VF_VRCPH = 0x32;
inline constexpr u32 VF_VMOV  = 0x33;
inline constexpr u32 VF_VRSQ  = 0x34;
inline constexpr u32 VF_VRSQL = 0x35;
inline constexpr u32 VF_VRSQH = 0x36;
inline constexpr u32 VF_VNOP  = 0x37;

inline constexpr u32 VMEM_LBV = 0x00;
inline constexpr u32 VMEM_LSV = 0x01;
inline constexpr u32 VMEM_LLV = 0x02;
inline constexpr u32 VMEM_LDV = 0x03;
inline constexpr u32 VMEM_LQV = 0x04;
inline constexpr u32 VMEM_LRV = 0x05;
inline constexpr u32 VMEM_LPV = 0x06;
inline constexpr u32 VMEM_LUV = 0x07;
inline constexpr u32 VMEM_LHV = 0x08;
inline constexpr u32 VMEM_LFV = 0x09;
inline constexpr u32 VMEM_LWV = 0x0A;
inline constexpr u32 VMEM_LTV = 0x0B;

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

[[nodiscard]] inline constexpr u32 cop2_transfer(
    u32 subop, u32 rt, u32 vd, u32 element = 0) noexcept {
    return (COP2 << 26) | ((subop & 31) << 21) | ((rt & 31) << 16) |
           ((vd & 31) << 11) | ((element & 15) << 7);
}
[[nodiscard]] inline constexpr u32 mfc2(u32 rt, u32 vd, u32 element = 0) noexcept {
    return cop2_transfer(COP2_MFC, rt, vd, element);
}
[[nodiscard]] inline constexpr u32 cfc2(u32 rt, u32 control) noexcept {
    return cop2_transfer(COP2_CFC, rt, control);
}
[[nodiscard]] inline constexpr u32 mtc2(u32 rt, u32 vd, u32 element = 0) noexcept {
    return cop2_transfer(COP2_MTC, rt, vd, element);
}
[[nodiscard]] inline constexpr u32 ctc2(u32 rt, u32 control) noexcept {
    return cop2_transfer(COP2_CTC, rt, control);
}

[[nodiscard]] inline constexpr u32 vector(
    u32 function, u32 vd, u32 vs, u32 vt, u32 element = 0) noexcept {
    return (COP2 << 26) | ((0x10u | (element & 15)) << 21) |
           ((vt & 31) << 16) | ((vs & 31) << 11) | ((vd & 31) << 6) |
           (function & 63);
}

[[nodiscard]] inline constexpr u32 vector_memory(
    bool store, u32 subop, u32 vt, u32 base, u32 element, s8 offset) noexcept {
    return ((store ? SWC2 : LWC2) << 26) | ((base & 31) << 21) |
           ((vt & 31) << 16) | ((subop & 31) << 11) |
           ((element & 15) << 7) | (static_cast<u8>(offset) & 0x7Fu);
}
[[nodiscard]] inline constexpr u32 lbv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(false, VMEM_LBV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 lsv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(false, VMEM_LSV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 llv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(false, VMEM_LLV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 ldv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(false, VMEM_LDV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 lqv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(false, VMEM_LQV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 lrv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(false, VMEM_LRV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 sbv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LBV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 ssv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LSV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 slv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LLV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 sdv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LDV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 sqv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LQV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 srv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LRV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 lpv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(false, VMEM_LPV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 luv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(false, VMEM_LUV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 lhv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(false, VMEM_LHV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 lfv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(false, VMEM_LFV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 ltv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(false, VMEM_LTV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 spv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LPV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 suv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LUV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 shv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LHV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 sfv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LFV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 swv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LWV, vt, base, element, off);
}
[[nodiscard]] inline constexpr u32 stv(u32 vt, u32 element, s8 off, u32 base) noexcept {
    return vector_memory(true, VMEM_LTV, vt, base, element, off);
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
