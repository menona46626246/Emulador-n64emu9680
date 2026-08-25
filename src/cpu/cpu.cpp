#include "n64/cpu/cpu.hpp"

#include "n64/bus/bus.hpp"
#include "n64/common/log.hpp"
#include "n64/cpu/block_cache.hpp"

#include <cstdio>
#include <limits>

namespace n64 {
namespace {

// Primary opcodes
constexpr u32 OP_SPECIAL = 0x00;
constexpr u32 OP_REGIMM  = 0x01;
constexpr u32 OP_J       = 0x02;
constexpr u32 OP_JAL     = 0x03;
constexpr u32 OP_BEQ     = 0x04;
constexpr u32 OP_BNE     = 0x05;
constexpr u32 OP_BLEZ    = 0x06;
constexpr u32 OP_BGTZ    = 0x07;
constexpr u32 OP_ADDI    = 0x08;
constexpr u32 OP_ADDIU   = 0x09;
constexpr u32 OP_SLTI    = 0x0A;
constexpr u32 OP_SLTIU   = 0x0B;
constexpr u32 OP_ANDI    = 0x0C;
constexpr u32 OP_ORI     = 0x0D;
constexpr u32 OP_XORI    = 0x0E;
constexpr u32 OP_LUI     = 0x0F;
constexpr u32 OP_COP0    = 0x10;
constexpr u32 OP_COP1    = 0x11;
constexpr u32 OP_COP2    = 0x12;
constexpr u32 OP_BEQL    = 0x14;
constexpr u32 OP_BNEL    = 0x15;
constexpr u32 OP_BLEZL   = 0x16;
constexpr u32 OP_BGTZL   = 0x17;
constexpr u32 OP_DADDI   = 0x18;
constexpr u32 OP_DADDIU  = 0x19;
constexpr u32 OP_LDL     = 0x1A;
constexpr u32 OP_LDR     = 0x1B;
constexpr u32 OP_LB      = 0x20;
constexpr u32 OP_LH      = 0x21;
constexpr u32 OP_LWL     = 0x22;
constexpr u32 OP_LW      = 0x23;
constexpr u32 OP_LBU     = 0x24;
constexpr u32 OP_LHU     = 0x25;
constexpr u32 OP_LWR     = 0x26;
constexpr u32 OP_LWU     = 0x27;
constexpr u32 OP_SB      = 0x28;
constexpr u32 OP_SH      = 0x29;
constexpr u32 OP_SWL     = 0x2A;
constexpr u32 OP_SW      = 0x2B;
constexpr u32 OP_SDL     = 0x2C;
constexpr u32 OP_SDR     = 0x2D;
constexpr u32 OP_SWR     = 0x2E;
constexpr u32 OP_CACHE   = 0x2F;
constexpr u32 OP_LL      = 0x30;
constexpr u32 OP_LWC1    = 0x31;
constexpr u32 OP_LLD     = 0x34;
constexpr u32 OP_LDC1    = 0x35;
constexpr u32 OP_LD      = 0x37;
constexpr u32 OP_SC      = 0x38;
constexpr u32 OP_SWC1    = 0x39;
constexpr u32 OP_SCD     = 0x3C;
constexpr u32 OP_SDC1    = 0x3D;
constexpr u32 OP_SD      = 0x3F;

// SPECIAL function codes
constexpr u32 FN_SLL     = 0x00;
constexpr u32 FN_SRL     = 0x02;
constexpr u32 FN_SRA     = 0x03;
constexpr u32 FN_SLLV    = 0x04;
constexpr u32 FN_SRLV    = 0x06;
constexpr u32 FN_SRAV    = 0x07;
constexpr u32 FN_JR      = 0x08;
constexpr u32 FN_JALR    = 0x09;
constexpr u32 FN_SYSCALL = 0x0C;
constexpr u32 FN_BREAK   = 0x0D;
constexpr u32 FN_SYNC    = 0x0F;
constexpr u32 FN_MFHI    = 0x10;
constexpr u32 FN_MTHI    = 0x11;
constexpr u32 FN_MFLO    = 0x12;
constexpr u32 FN_MTLO    = 0x13;
constexpr u32 FN_DSLLV   = 0x14;
constexpr u32 FN_DSRLV   = 0x16;
constexpr u32 FN_DSRAV   = 0x17;
constexpr u32 FN_MULT    = 0x18;
constexpr u32 FN_MULTU   = 0x19;
constexpr u32 FN_DIV     = 0x1A;
constexpr u32 FN_DIVU    = 0x1B;
constexpr u32 FN_DMULT   = 0x1C;
constexpr u32 FN_DMULTU  = 0x1D;
constexpr u32 FN_DDIV    = 0x1E;
constexpr u32 FN_DDIVU   = 0x1F;
constexpr u32 FN_ADD     = 0x20;
constexpr u32 FN_ADDU    = 0x21;
constexpr u32 FN_SUB     = 0x22;
constexpr u32 FN_SUBU    = 0x23;
constexpr u32 FN_AND     = 0x24;
constexpr u32 FN_OR      = 0x25;
constexpr u32 FN_XOR     = 0x26;
constexpr u32 FN_NOR     = 0x27;
constexpr u32 FN_SLT     = 0x2A;
constexpr u32 FN_SLTU    = 0x2B;
constexpr u32 FN_DADD    = 0x2C;
constexpr u32 FN_DADDU   = 0x2D;
constexpr u32 FN_DSUB    = 0x2E;
constexpr u32 FN_DSUBU   = 0x2F;
constexpr u32 FN_TGE     = 0x30;
constexpr u32 FN_TGEU    = 0x31;
constexpr u32 FN_TLT     = 0x32;
constexpr u32 FN_TLTU    = 0x33;
constexpr u32 FN_TEQ     = 0x34;
constexpr u32 FN_TNE     = 0x36;
constexpr u32 FN_DSLL    = 0x38;
constexpr u32 FN_DSRL    = 0x3A;
constexpr u32 FN_DSRA    = 0x3B;
constexpr u32 FN_DSLL32  = 0x3C;
constexpr u32 FN_DSRL32  = 0x3E;
constexpr u32 FN_DSRA32  = 0x3F;

// REGIMM rt codes
constexpr u32 RT_BLTZ    = 0x00;
constexpr u32 RT_BGEZ    = 0x01;
constexpr u32 RT_BLTZL   = 0x02;
constexpr u32 RT_BGEZL   = 0x03;
constexpr u32 RT_TGEI    = 0x08;
constexpr u32 RT_TGEIU   = 0x09;
constexpr u32 RT_TLTI    = 0x0A;
constexpr u32 RT_TLTIU   = 0x0B;
constexpr u32 RT_TEQI    = 0x0C;
constexpr u32 RT_TNEI    = 0x0E;
constexpr u32 RT_BLTZAL  = 0x10;
constexpr u32 RT_BGEZAL  = 0x11;
constexpr u32 RT_BLTZALL = 0x12;
constexpr u32 RT_BGEZALL = 0x13;

// Status bits
constexpr u32 SR_IE   = 1u << 0;
constexpr u32 SR_EXL  = 1u << 1;
constexpr u32 SR_ERL  = 1u << 2;
constexpr u32 SR_KSU  = 0x18u;
constexpr u32 SR_BEV  = 1u << 22;
constexpr u32 CAUSE_BD = 1u << 31;
constexpr u32 CAUSE_IP2 = 1u << 10;
constexpr u32 CAUSE_IP7 = 1u << 15;

[[nodiscard]] bool add_overflow_s32(s32 a, s32 b, s32& result) noexcept {
    const s64 wide = static_cast<s64>(a) + static_cast<s64>(b);
    result = static_cast<s32>(wide);
    return wide != static_cast<s64>(result);
}

[[nodiscard]] bool sub_overflow_s32(s32 a, s32 b, s32& result) noexcept {
    const s64 wide = static_cast<s64>(a) - static_cast<s64>(b);
    result = static_cast<s32>(wide);
    return wide != static_cast<s64>(result);
}

[[nodiscard]] bool add_overflow_s64(s64 a, s64 b, s64& result) noexcept {
    if ((b > 0 && a > std::numeric_limits<s64>::max() - b) ||
        (b < 0 && a < std::numeric_limits<s64>::min() - b)) {
        result = 0;
        return true;
    }
    result = a + b;
    return false;
}

[[nodiscard]] bool sub_overflow_s64(s64 a, s64 b, s64& result) noexcept {
    if ((b > 0 && a < std::numeric_limits<s64>::min() + b) ||
        (b < 0 && a > std::numeric_limits<s64>::max() + b)) {
        result = 0;
        return true;
    }
    result = a - b;
    return false;
}

void mult_u64(u64 a, u64 b, u64& hi, u64& lo) noexcept {
#if defined(__SIZEOF_INT128__)
    const __uint128_t r = static_cast<__uint128_t>(a) * static_cast<__uint128_t>(b);
    lo = static_cast<u64>(r);
    hi = static_cast<u64>(r >> 64);
#else
    const u64 a_lo = a & 0xFFFFFFFFu, a_hi = a >> 32;
    const u64 b_lo = b & 0xFFFFFFFFu, b_hi = b >> 32;
    const u64 p0 = a_lo * b_lo, p1 = a_lo * b_hi, p2 = a_hi * b_lo, p3 = a_hi * b_hi;
    const u64 mid = (p0 >> 32) + (p1 & 0xFFFFFFFFu) + (p2 & 0xFFFFFFFFu);
    lo = (p0 & 0xFFFFFFFFu) | (mid << 32);
    hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
#endif
}

void mult_s64(s64 a, s64 b, u64& hi, u64& lo) noexcept {
#if defined(__SIZEOF_INT128__)
    const __int128_t r = static_cast<__int128_t>(a) * static_cast<__int128_t>(b);
    lo = static_cast<u64>(static_cast<__uint128_t>(r));
    hi = static_cast<u64>(static_cast<__uint128_t>(r) >> 64);
#else
    const bool neg = (a < 0) != (b < 0);
    const auto magnitude = [](s64 value) noexcept {
        const u64 bits = static_cast<u64>(value);
        return value < 0 ? (~bits + 1u) : bits;
    };
    mult_u64(magnitude(a), magnitude(b), hi, lo);
    if (neg) {
        lo = ~lo + 1;
        hi = ~hi + (lo == 0 ? 1u : 0u);
    }
#endif
}

} // namespace

// =============================================================================
// Lifecycle
// =============================================================================

void Cpu::reset() {
    gpr_.fill(0);
    cop0_.fill(0);
    pc_ = 0xBFC0'0000ull;
    next_pc_ = pc_ + 4;
    hi_ = lo_ = 0;
    cycles_ = 0;
    halted_ = false;
    in_delay_slot_ = false;
    branch_pending_ = false;
    ll_bit_ = false;
    ll_addr_ = 0;
    exception_count_ = 0;
    unknown_opcode_count_ = 0;

    cop0_[Cop0Reg::Random]  = 31;
    cop0_[Cop0Reg::Status]  = 0x3400'0000u; // BEV=1
    cop0_[Cop0Reg::PRId]    = 0x0000'0B22u;
    cop0_[Cop0Reg::Config]  = 0x7006'E463u;
    cop0_[Cop0Reg::Count]   = 0;
    cop0_[Cop0Reg::Compare] = 0;
    cop0_[Cop0Reg::Wired]   = 0;

    N64_DEBUG("CPU reset, PC={:08X}", static_cast<u32>(pc_));
}

void Cpu::set_cop0(std::size_t i, u32 v) noexcept {
    const u32 idx = static_cast<u32>(i & 31);
    switch (idx) {
    case Cop0Reg::Random:
    case Cop0Reg::PRId:
        return;
    case Cop0Reg::Compare:
        cop0_[idx] = v;
        cop0_[Cop0Reg::Cause] &= ~(1u << 15);
        return;
    case Cop0Reg::Cause:
        cop0_[idx] = (cop0_[idx] & ~0x300u) | (v & 0x300u);
        return;
    default:
        cop0_[idx] = v;
        return;
    }
}

Cycles Cpu::run(Cycles n) {
    Cycles taken = 0;
    while (taken < n && !halted_) {
        if (block_cache_ && block_cache_enabled_ && !branch_pending_ && !trace_ && bus_) {
            const BasicBlock* blk = block_cache_->lookup(pc_);
            if (!blk) {
                blk = block_cache_->compile(pc_, *bus_, *this);
            }
            if (blk && !blk->insns.empty() && (taken + blk->insns.size() <= static_cast<std::size_t>(n))) {
                const Cycles did = execute_block(*blk);
                taken += did;
                if (did == 0) {
                    step();
                    ++taken;
                }
                continue;
            }
        }
        step();
        ++taken;
    }
    return taken;
}

Cycles Cpu::execute_block(const BasicBlock& block) {
    const std::size_t n = block.insns.size();
    if (n == 0) return 0;

    Cycles did = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const u64 expected_pc = block.start_pc + static_cast<u64>(i * 4);
        if (pc_ != expected_pc) {
            // Branch-likely not taken nullifies its cached delay slot.
            return did;
        }
        if (!branch_pending_) {
            const u32 sr = cop0_[Cop0Reg::Status];
            if ((sr & SR_IE) != 0 && (sr & (SR_EXL | SR_ERL)) == 0) {
                const u32 cause = cop0_[Cop0Reg::Cause];
                if ((cause & sr & 0xFF00u) != 0) {
                    const u64 exc_before_irq = exception_count_;
                    check_interrupts();
                    if (exception_count_ != exc_before_irq) {
                        gpr_[0] = 0;
                        advance_count();
                        ++cycles_;
                        in_delay_slot_ = false;
                        return did + 1;
                    }
                }
            }
        }

        const u64 insn_pc = pc_;
        const bool delay_slot = branch_pending_;
        in_delay_slot_ = delay_slot;

        if (!delay_slot) {
            next_pc_ = insn_pc + 4;
            branch_pending_ = false;
        }

        if (trace_) {
            trace_(insn_pc, block.insns[i], disassemble(block.insns[i]));
        }

        const u64 exc_before = exception_count_;
        execute(block.insns[i]);

        if (exception_count_ != exc_before) {
            gpr_[0] = 0;
            advance_count();
            ++cycles_;
            in_delay_slot_ = false;
            branch_pending_ = false;
            return did + 1;
        }

        if (delay_slot) {
            pc_ = next_pc_;
            next_pc_ = pc_ + 4;
            branch_pending_ = false;
        } else if (branch_pending_) {
            const u64 target = next_pc_;
            pc_ = insn_pc + 4;
            next_pc_ = target;
        } else {
            pc_ = next_pc_;
            next_pc_ = pc_ + 4;
        }

        gpr_[0] = 0;
        advance_count();
        ++cycles_;
        in_delay_slot_ = false;
        ++did;

        if (halted_) {
            break;
        }
    }
    return did;
}

// =============================================================================
// Address translation
// =============================================================================

bool Cpu::translate(u64 vaddr, bool /*is_store*/, PhysicalAddress& out_paddr) const {
    const u32 v = static_cast<u32>(vaddr);

    // KSEG0 cached
    if (v >= 0x8000'0000u && v <= 0x9FFF'FFFFu) {
        out_paddr = v & 0x1FFF'FFFFu;
        return true;
    }
    // KSEG1 uncached
    if (v >= 0xA000'0000u && v <= 0xBFFF'FFFFu) {
        out_paddr = v & 0x1FFF'FFFFu;
        return true;
    }
    // KUSEG: identity-map low 512 MiB for synthetic tests (no TLB yet).
    // INCÓGNITA U008 — real hardware uses TLB.
    if (v <= 0x7FFF'FFFFu) {
        out_paddr = v & 0x1FFF'FFFFu;
        return true;
    }
    return false;
}

bool Cpu::kernel_mode() const noexcept {
    const u32 sr = cop0_[Cop0Reg::Status];
    if (sr & (SR_EXL | SR_ERL)) {
        return true;
    }
    return ((sr & SR_KSU) >> 3) == 0;
}

bool Cpu::coprocessor_usable(u32 cop) const noexcept {
    if (cop == 0 && kernel_mode()) {
        return true;
    }
    const u32 sr = cop0_[Cop0Reg::Status];
    return (sr & (1u << (28 + cop))) != 0;
}

void Cpu::advance_count() {
    ++cop0_[Cop0Reg::Count];
    if (cop0_[Cop0Reg::Count] == cop0_[Cop0Reg::Compare]) {
        cop0_[Cop0Reg::Cause] |= CAUSE_IP7;
    }
}

void Cpu::set_rcp_interrupt(bool level) noexcept {
    if (level) {
        cop0_[Cop0Reg::Cause] |= CAUSE_IP2;
    } else {
        cop0_[Cop0Reg::Cause] &= ~CAUSE_IP2;
    }
}

void Cpu::check_interrupts() {
    const u32 sr = cop0_[Cop0Reg::Status];
    if ((sr & SR_IE) == 0) return;
    if ((sr & (SR_EXL | SR_ERL)) != 0) return;

    const u32 cause = cop0_[Cop0Reg::Cause];
    const u32 pending = (cause & sr & 0xFF00u); // IM field overlaps IP bits 15:8
    if (pending != 0) {
        raise_exception(ExcCode::Int);
    }
}

// =============================================================================
// Exceptions
// =============================================================================

void Cpu::raise_exception(u32 exc_code, u64 bad_vaddr, u32 ce) {
    ++exception_count_;

    u32 cause = cop0_[Cop0Reg::Cause];
    cause = (cause & ~0x7Cu) | ((exc_code & 0x1Fu) << 2);
    cause = (cause & ~(0x3u << 28)) | ((ce & 0x3u) << 28);

    if (in_delay_slot_) {
        cause |= CAUSE_BD;
        cop0_[Cop0Reg::EPC] = static_cast<u32>(pc_ - 4);
    } else {
        cause &= ~CAUSE_BD;
        cop0_[Cop0Reg::EPC] = static_cast<u32>(pc_);
    }
    cop0_[Cop0Reg::Cause] = cause;

    if (exc_code == ExcCode::AdEL || exc_code == ExcCode::AdES ||
        exc_code == ExcCode::TLBL || exc_code == ExcCode::TLBS ||
        exc_code == ExcCode::Mod) {
        cop0_[Cop0Reg::BadVAddr] = static_cast<u32>(bad_vaddr);
    }

    const u32 sr = cop0_[Cop0Reg::Status];
    cop0_[Cop0Reg::Status] = sr | SR_EXL;

    const bool bev = (sr & SR_BEV) != 0;
    u64 vector;
    if ((exc_code == ExcCode::TLBL || exc_code == ExcCode::TLBS) && !(sr & SR_EXL)) {
        vector = bev ? 0xBFC0'0200ull : 0x8000'0000ull;
    } else {
        vector = bev ? 0xBFC0'0380ull : 0x8000'0180ull;
    }

    // Redirect fetch stream; step() will not advance PC after exception.
    pc_ = vector;
    next_pc_ = vector + 4;
    branch_pending_ = false;
    // Keep in_delay_slot_ as-is for EPC already computed; clear after.
    ll_bit_ = false;

    N64_DEBUG("Exception code={} EPC={:08X} → {:08X}",
              exc_code, cop0_[Cop0Reg::EPC], static_cast<u32>(vector));
}

// =============================================================================
// Memory
// =============================================================================

u32 Cpu::fetch(u64 vaddr) {
    if ((vaddr & 3ull) != 0) {
        raise_exception(ExcCode::AdEL, vaddr);
        return 0;
    }
    PhysicalAddress paddr = 0;
    if (!translate(vaddr, false, paddr)) {
        raise_exception(ExcCode::TLBL, vaddr);
        return 0;
    }
    if (!bus_) {
        N64_ERROR("CPU fetch with no bus");
        halted_ = true;
        return 0;
    }
    return bus_->read32(paddr);
}

bool Cpu::probe_read(u64 vaddr, int size, PhysicalAddress& paddr) {
    if ((vaddr & static_cast<u64>(size - 1)) != 0) {
        raise_exception(ExcCode::AdEL, vaddr);
        return false;
    }
    if (!translate(vaddr, false, paddr)) {
        raise_exception(ExcCode::TLBL, vaddr);
        return false;
    }
    return bus_ != nullptr;
}

bool Cpu::probe_write(u64 vaddr, int size, PhysicalAddress& paddr) {
    if ((vaddr & static_cast<u64>(size - 1)) != 0) {
        raise_exception(ExcCode::AdES, vaddr);
        return false;
    }
    if (!translate(vaddr, true, paddr)) {
        raise_exception(ExcCode::TLBS, vaddr);
        return false;
    }
    return bus_ != nullptr;
}

bool Cpu::load_byte(u64 vaddr, bool sign_extend, u64& value) {
    PhysicalAddress p = 0;
    if (!probe_read(vaddr, 1, p)) return false;
    const u8 v = bus_->read8(p);
    value = sign_extend ? sext8(v) : static_cast<u64>(v);
    return true;
}

bool Cpu::load_half(u64 vaddr, bool sign_extend, u64& value) {
    PhysicalAddress p = 0;
    if (!probe_read(vaddr, 2, p)) return false;
    const u16 v = bus_->read16(p);
    value = sign_extend ? sext16(v) : static_cast<u64>(v);
    return true;
}

bool Cpu::load_word(u64 vaddr, bool sign_extend, u64& value) {
    PhysicalAddress p = 0;
    if (!probe_read(vaddr, 4, p)) return false;
    const u32 v = bus_->read32(p);
    value = sign_extend ? sext32(v) : static_cast<u64>(v);
    return true;
}

bool Cpu::load_double(u64 vaddr, u64& value) {
    PhysicalAddress p = 0;
    if (!probe_read(vaddr, 8, p)) return false;
    value = bus_->read64(p);
    return true;
}

void Cpu::store_byte(u64 vaddr, u8 value) {
    PhysicalAddress p = 0;
    if (!probe_write(vaddr, 1, p)) return;
    bus_->write8(p, value);
    ll_bit_ = false;
}

void Cpu::store_half(u64 vaddr, u16 value) {
    PhysicalAddress p = 0;
    if (!probe_write(vaddr, 2, p)) return;
    bus_->write16(p, value);
    ll_bit_ = false;
}

void Cpu::store_word(u64 vaddr, u32 value) {
    PhysicalAddress p = 0;
    if (!probe_write(vaddr, 4, p)) return;
    bus_->write32(p, value);
    ll_bit_ = false;
}

void Cpu::store_double(u64 vaddr, u64 value) {
    PhysicalAddress p = 0;
    if (!probe_write(vaddr, 8, p)) return;
    bus_->write64(p, value);
    ll_bit_ = false;
}

// Big-endian unaligned merge (MIPS IV / VR4300)
void Cpu::exec_lwl(u32 insn) {
    const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
    const u64 aligned = addr & ~0x3ull;
    PhysicalAddress p = 0;
    if (!translate(aligned, false, p) || !bus_) {
        raise_exception(ExcCode::TLBL, addr);
        return;
    }
    const u32 mem = bus_->read32(p);
    const u32 n = static_cast<u32>(addr & 3ull); // 0=leftmost byte
    const u32 mask = 0xFFFF'FFFFu << ((3u - n) * 8u);
    const u32 merged = mem << ((3u - n) * 8u);
    const u32 result = (rt32(insn) & ~mask) | (merged & mask);
    write_gpr(rt(insn), sext32(result));
}

void Cpu::exec_lwr(u32 insn) {
    const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
    const u64 aligned = addr & ~0x3ull;
    PhysicalAddress p = 0;
    if (!translate(aligned, false, p) || !bus_) {
        raise_exception(ExcCode::TLBL, addr);
        return;
    }
    const u32 mem = bus_->read32(p);
    const u32 n = static_cast<u32>(addr & 3ull);
    const u32 mask = 0xFFFF'FFFFu >> (n * 8u);
    const u32 merged = mem >> (n * 8u);
    const u32 result = (rt32(insn) & ~mask) | (merged & mask);
    write_gpr(rt(insn), sext32(result));
}

void Cpu::exec_swl(u32 insn) {
    const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
    const u64 aligned = addr & ~0x3ull;
    PhysicalAddress p = 0;
    if (!translate(aligned, true, p) || !bus_) {
        raise_exception(ExcCode::TLBS, addr);
        return;
    }
    const u32 n = static_cast<u32>(addr & 3ull);
    const u32 reg = rt32(insn);
    u32 mem = bus_->read32(p);
    const u32 mask = 0xFFFF'FFFFu << ((3u - n) * 8u);
    mem = (mem & ~mask) | ((reg >> ((3u - n) * 8u)) & mask);
    bus_->write32(p, mem);
    ll_bit_ = false;
}

void Cpu::exec_swr(u32 insn) {
    const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
    const u64 aligned = addr & ~0x3ull;
    PhysicalAddress p = 0;
    if (!translate(aligned, true, p) || !bus_) {
        raise_exception(ExcCode::TLBS, addr);
        return;
    }
    const u32 n = static_cast<u32>(addr & 3ull);
    const u32 reg = rt32(insn);
    u32 mem = bus_->read32(p);
    const u32 mask = 0xFFFF'FFFFu >> (n * 8u);
    mem = (mem & ~mask) | ((reg << (n * 8u)) & mask);
    bus_->write32(p, mem);
    ll_bit_ = false;
}

void Cpu::exec_ldl(u32 insn) {
    const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
    const u64 aligned = addr & ~0x7ull;
    PhysicalAddress p = 0;
    if (!translate(aligned, false, p) || !bus_) {
        raise_exception(ExcCode::TLBL, addr);
        return;
    }
    const u64 mem = bus_->read64(p);
    const u32 n = static_cast<u32>(addr & 7ull);
    const u64 mask = ~0ull << ((7u - n) * 8u);
    const u64 merged = mem << ((7u - n) * 8u);
    write_gpr(rt(insn), (rt64(insn) & ~mask) | (merged & mask));
}

void Cpu::exec_ldr(u32 insn) {
    const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
    const u64 aligned = addr & ~0x7ull;
    PhysicalAddress p = 0;
    if (!translate(aligned, false, p) || !bus_) {
        raise_exception(ExcCode::TLBL, addr);
        return;
    }
    const u64 mem = bus_->read64(p);
    const u32 n = static_cast<u32>(addr & 7ull);
    const u64 mask = ~0ull >> (n * 8u);
    const u64 merged = mem >> (n * 8u);
    write_gpr(rt(insn), (rt64(insn) & ~mask) | (merged & mask));
}

void Cpu::exec_sdl(u32 insn) {
    const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
    const u64 aligned = addr & ~0x7ull;
    PhysicalAddress p = 0;
    if (!translate(aligned, true, p) || !bus_) {
        raise_exception(ExcCode::TLBS, addr);
        return;
    }
    const u32 n = static_cast<u32>(addr & 7ull);
    const u64 reg = rt64(insn);
    u64 mem = bus_->read64(p);
    const u64 mask = ~0ull << ((7u - n) * 8u);
    mem = (mem & ~mask) | ((reg >> ((7u - n) * 8u)) & mask);
    bus_->write64(p, mem);
    ll_bit_ = false;
}

void Cpu::exec_sdr(u32 insn) {
    const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
    const u64 aligned = addr & ~0x7ull;
    PhysicalAddress p = 0;
    if (!translate(aligned, true, p) || !bus_) {
        raise_exception(ExcCode::TLBS, addr);
        return;
    }
    const u32 n = static_cast<u32>(addr & 7ull);
    const u64 reg = rt64(insn);
    u64 mem = bus_->read64(p);
    const u64 mask = ~0ull >> (n * 8u);
    mem = (mem & ~mask) | ((reg << (n * 8u)) & mask);
    bus_->write64(p, mem);
    ll_bit_ = false;
}

// =============================================================================
// Branch helpers
//
// Delay-slot model:
//   pc_      = instruction being executed
//   next_pc_ = PC after this instruction (delay slot or sequential)
//   On taken branch: set next_pc_ = pc_+4 (delay slot) ONLY if not already
//   sequential, and stash target so AFTER the delay slot we go there.
//
// Implementation: branch_pending_ means "next_pc_ is the delay slot, and
// after executing it we must load the branch target".
// We store the branch target by temporarily putting it in... we need a field.
//
// Looking at the header: we have next_pc_ and branch_pending_.
// Clean approach used by many emulators:
//   At start: nothing special
//   branch taken:  next_pc_ = target;  but we execute delay first by:
//     pc stays, after insn pc becomes old_pc+4, and we set a flag that
//     the FOLLOWING next is target.
//
// Simpler two-pointer scheme used here:
//   pc_ = current
//   next_pc_ = next to execute after current
//   When branch taken during execute:
//     // next_pc_ currently equals pc_+4 (set at start of step) = delay slot
//     // We want after delay to go to target. So:
//     // Save: after delay, next_pc should become target.
//     // Trick: set branch_pending_ and OVERWRITE by storing target in a way
//     // that end-of-step does:
//     //   pc_ = next_pc_ (delay slot)
//     //   next_pc_ = target   if branch_pending_
//
// So branch_abs/rel should SET a target and leave next_pc_ as delay (pc+4).
// =============================================================================

void Cpu::branch_abs(u64 target_pc) {
    // next_pc_ already holds delay slot address (pc+4) from step() prologue.
    // Stash target: we'll put it into next_pc_ AFTER moving pc to delay slot.
    // Use: set next_pc_ to delay (already), mark pending, store target in...
    // We reuse: after execute, if branch_pending_, then:
    //   pc_ = pc_ + 4 (delay)  -- wait we need the target stored.
    //
    // Store target by writing next_pc_ = target and remembering delay separately.
    // Actually the cleanest with existing fields:
    //   branch_pending_ = true
    //   next_pc_ = target   // the eventual target
    //   And step end does:
    //     if (branch was just taken this instruction):
    //       // go to delay slot first
    //       u64 delay = insn_pc + 4;
    //       u64 target = next_pc_;
    //       pc_ = delay;
    //       next_pc_ = target;
    //       branch_pending_ = true meaning "next insn is delay slot"
    //
    // We'll handle this in step() by checking a "just_branched" concept.
    // Set next_pc_ to target; step() knows if branch_pending_ flipped to true
    // during execute, go to delay first.
    next_pc_ = target_pc;
    branch_pending_ = true;
}

void Cpu::branch_rel(s32 offset_imm) {
    const s64 off = static_cast<s64>(offset_imm) << 2;
    // Relative to address of delay slot (= pc + 4)
    next_pc_ = static_cast<u64>(static_cast<s64>(pc_ + 4) + off);
    branch_pending_ = true;
}

void Cpu::link(u32 reg) {
    write_gpr(reg, pc_ + 8);
}

// =============================================================================
// Step
// =============================================================================

void Cpu::step() {
    if (halted_) {
        ++cycles_;
        return;
    }

    // Service external / timer interrupts before fetch (not in delay slot mid-branch).
    // If an interrupt is taken, PC is redirected to the vector; end this step so
    // the first handler instruction is fetched on the next step.
    if (!branch_pending_) {
        const u64 exc_before_irq = exception_count_;
        check_interrupts();
        if (exception_count_ != exc_before_irq) {
            gpr_[0] = 0;
            advance_count();
            ++cycles_;
            in_delay_slot_ = false;
            return;
        }
    }

    const u64 insn_pc = pc_;
    // We are in a delay slot if the previous instruction was a taken branch
    // that set branch_pending_ and we already advanced pc to the delay slot.
    // Track via: if branch_pending_ is true at ENTRY, then next_pc_ is the
    // branch target and current pc is the delay slot.
    const bool delay_slot = branch_pending_;
    in_delay_slot_ = delay_slot;

    const u64 exc_before = exception_count_;
    const u32 insn = fetch(insn_pc);
    if (exception_count_ != exc_before) {
        gpr_[0] = 0;
        advance_count();
        ++cycles_;
        in_delay_slot_ = false;
        return;
    }

    // Default: sequential next. If we are in a delay slot, next_pc_ already
    // holds the branch target — do not overwrite it.
    if (!delay_slot) {
        next_pc_ = insn_pc + 4;
        branch_pending_ = false;
    }

    if (trace_) {
        trace_(insn_pc, insn, disassemble(insn));
    }

    // If a branch is taken, execute() calls branch_* which sets
    // next_pc_ = target and branch_pending_ = true.
    execute(insn);

    if (exception_count_ != exc_before) {
        // Exception redirected pc_/next_pc_.
        gpr_[0] = 0;
        advance_count();
        ++cycles_;
        in_delay_slot_ = false;
        branch_pending_ = false;
        return;
    }

    if (delay_slot) {
        // Finished delay slot → jump to branch target (already in next_pc_).
        pc_ = next_pc_;
        next_pc_ = pc_ + 4;
        branch_pending_ = false;
    } else if (branch_pending_) {
        // Branch just taken: execute delay slot next, keep target in next_pc_.
        const u64 target = next_pc_;
        pc_ = insn_pc + 4;       // delay slot
        next_pc_ = target;       // after delay
        // branch_pending_ stays true so next step knows it's a delay slot
    } else {
        // Sequential
        pc_ = next_pc_;
        next_pc_ = pc_ + 4;
    }

    gpr_[0] = 0;
    advance_count();
    ++cycles_;
    in_delay_slot_ = false;
}

// =============================================================================
// Execute dispatch
// =============================================================================

void Cpu::execute(u32 insn) {
    switch (op(insn)) {
    case OP_SPECIAL: exec_special(insn); break;
    case OP_REGIMM:  exec_regimm(insn);  break;

    case OP_J: {
        const u64 t = (pc_ & 0xFFFFFFFF'F0000000ull) |
                      (static_cast<u64>(target(insn)) << 2);
        branch_abs(t);
        break;
    }
    case OP_JAL: {
        link(31);
        const u64 t = (pc_ & 0xFFFFFFFF'F0000000ull) |
                      (static_cast<u64>(target(insn)) << 2);
        branch_abs(t);
        break;
    }

    case OP_BEQ:
        if (rs64(insn) == rt64(insn)) {
            branch_rel(simm(insn));
        } else {
            branch_abs(pc_ + 8);
        }
        break;
    case OP_BNE:
        if (rs64(insn) != rt64(insn)) {
            branch_rel(simm(insn));
        } else {
            branch_abs(pc_ + 8);
        }
        break;
    case OP_BLEZ:
        if (rs64s(insn) <= 0) {
            branch_rel(simm(insn));
        } else {
            branch_abs(pc_ + 8);
        }
        break;
    case OP_BGTZ:
        if (rs64s(insn) > 0) {
            branch_rel(simm(insn));
        } else {
            branch_abs(pc_ + 8);
        }
        break;

    // Likely branches: skip delay slot if not taken
    case OP_BEQL:
        if (rs64(insn) == rt64(insn)) {
            branch_rel(simm(insn));
        } else {
            // Nullify delay slot by skipping it
            next_pc_ = pc_ + 8;
            branch_pending_ = false;
            // Force step end to go to pc+8: set as if sequential to pc+8
            // We'll set next_pc_ and clear pending; step sees !branch_pending_
            // and does pc_ = next_pc_. So set next_pc_ = pc+8.
        }
        break;
    case OP_BNEL:
        if (rs64(insn) != rt64(insn)) {
            branch_rel(simm(insn));
        } else {
            next_pc_ = pc_ + 8;
            branch_pending_ = false;
        }
        break;
    case OP_BLEZL:
        if (rs64s(insn) <= 0) {
            branch_rel(simm(insn));
        } else {
            next_pc_ = pc_ + 8;
            branch_pending_ = false;
        }
        break;
    case OP_BGTZL:
        if (rs64s(insn) > 0) {
            branch_rel(simm(insn));
        } else {
            next_pc_ = pc_ + 8;
            branch_pending_ = false;
        }
        break;

    case OP_ADDI: {
        s32 result = 0;
        if (add_overflow_s32(rs32s(insn), static_cast<s32>(simm(insn)), result)) {
            raise_exception(ExcCode::Ov);
        } else {
            write_gpr(rt(insn), sext32(static_cast<u32>(result)));
        }
        break;
    }
    case OP_ADDIU: {
        const u32 result = rs32(insn) + static_cast<u32>(static_cast<s32>(simm(insn)));
        write_gpr(rt(insn), sext32(result));
        break;
    }
    case OP_SLTI:
        write_gpr(rt(insn), rs64s(insn) < static_cast<s64>(simm(insn)) ? 1ull : 0ull);
        break;
    case OP_SLTIU:
        write_gpr(rt(insn), rs64(insn) < static_cast<u64>(static_cast<s64>(simm(insn))) ? 1ull : 0ull);
        break;
    case OP_ANDI:
        write_gpr(rt(insn), rs64(insn) & static_cast<u64>(imm(insn)));
        break;
    case OP_ORI:
        write_gpr(rt(insn), rs64(insn) | static_cast<u64>(imm(insn)));
        break;
    case OP_XORI:
        write_gpr(rt(insn), rs64(insn) ^ static_cast<u64>(imm(insn)));
        break;
    case OP_LUI:
        write_gpr(rt(insn), sext32(static_cast<u32>(imm(insn)) << 16));
        break;

    case OP_COP0: exec_cop0(insn); break;
    case OP_COP1: exec_cop1(insn); break;
    case OP_COP2: exec_cop2(insn); break;

    case OP_DADDI: {
        s64 result = 0;
        if (add_overflow_s64(rs64s(insn), static_cast<s64>(simm(insn)), result)) {
            raise_exception(ExcCode::Ov);
        } else {
            write_gpr(rt(insn), static_cast<u64>(result));
        }
        break;
    }
    case OP_DADDIU:
        write_gpr(rt(insn), static_cast<u64>(rs64s(insn) + static_cast<s64>(simm(insn))));
        break;

    case OP_LDL: exec_ldl(insn); break;
    case OP_LDR: exec_ldr(insn); break;

    case OP_LB: {
        u64 value = 0;
        if (load_byte(rs64(insn) + static_cast<s64>(simm(insn)), true, value)) {
            write_gpr(rt(insn), value);
        }
        break;
    }
    case OP_LH: {
        u64 value = 0;
        if (load_half(rs64(insn) + static_cast<s64>(simm(insn)), true, value)) {
            write_gpr(rt(insn), value);
        }
        break;
    }
    case OP_LWL: exec_lwl(insn); break;
    case OP_LW: {
        u64 value = 0;
        if (load_word(rs64(insn) + static_cast<s64>(simm(insn)), true, value)) {
            write_gpr(rt(insn), value);
        }
        break;
    }
    case OP_LBU: {
        u64 value = 0;
        if (load_byte(rs64(insn) + static_cast<s64>(simm(insn)), false, value)) {
            write_gpr(rt(insn), value);
        }
        break;
    }
    case OP_LHU: {
        u64 value = 0;
        if (load_half(rs64(insn) + static_cast<s64>(simm(insn)), false, value)) {
            write_gpr(rt(insn), value);
        }
        break;
    }
    case OP_LWR: exec_lwr(insn); break;
    case OP_LWU: {
        u64 value = 0;
        if (load_word(rs64(insn) + static_cast<s64>(simm(insn)), false, value)) {
            write_gpr(rt(insn), value);
        }
        break;
    }

    case OP_SB:
        store_byte(rs64(insn) + static_cast<s64>(simm(insn)),
                   static_cast<u8>(rt64(insn)));
        break;
    case OP_SH:
        store_half(rs64(insn) + static_cast<s64>(simm(insn)),
                   static_cast<u16>(rt64(insn)));
        break;
    case OP_SWL: exec_swl(insn); break;
    case OP_SW:
        store_word(rs64(insn) + static_cast<s64>(simm(insn)), rt32(insn));
        break;
    case OP_SDL: exec_sdl(insn); break;
    case OP_SDR: exec_sdr(insn); break;
    case OP_SWR: exec_swr(insn); break;

    case OP_CACHE:
        // CACHE ops are no-ops in the interpreter (no cache model yet).
        break;

    case OP_LL: {
        const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
        PhysicalAddress paddr = 0;
        if (probe_read(addr, 4, paddr)) {
            write_gpr(rt(insn), sext32(bus_->read32(paddr)));
            ll_bit_ = true;
            ll_addr_ = paddr;
        }
        break;
    }
    case OP_LLD: {
        const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
        PhysicalAddress paddr = 0;
        if (probe_read(addr, 8, paddr)) {
            write_gpr(rt(insn), bus_->read64(paddr));
            ll_bit_ = true;
            ll_addr_ = paddr;
        }
        break;
    }
    case OP_LD: {
        u64 value = 0;
        if (load_double(rs64(insn) + static_cast<s64>(simm(insn)), value)) {
            write_gpr(rt(insn), value);
        }
        break;
    }

    case OP_SC: {
        const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
        const bool had_reservation = ll_bit_;
        const u64 reserved_addr = ll_addr_;
        ll_bit_ = false;

        PhysicalAddress paddr = 0;
        if (!probe_write(addr, 4, paddr)) {
            break;
        }
        if (had_reservation && paddr == reserved_addr) {
            bus_->write32(paddr, rt32(insn));
            write_gpr(rt(insn), 1);
        } else {
            write_gpr(rt(insn), 0);
        }
        break;
    }
    case OP_SCD: {
        const u64 addr = rs64(insn) + static_cast<s64>(simm(insn));
        const bool had_reservation = ll_bit_;
        const u64 reserved_addr = ll_addr_;
        ll_bit_ = false;

        PhysicalAddress paddr = 0;
        if (!probe_write(addr, 8, paddr)) {
            break;
        }
        if (had_reservation && paddr == reserved_addr) {
            bus_->write64(paddr, rt64(insn));
            write_gpr(rt(insn), 1);
        } else {
            write_gpr(rt(insn), 0);
        }
        break;
    }
    case OP_SD:
        store_double(rs64(insn) + static_cast<s64>(simm(insn)), rt64(insn));
        break;

    case OP_LWC1:
    case OP_LDC1:
    case OP_SWC1:
    case OP_SDC1:
        // FPU load/store — require CU1
        if (!coprocessor_usable(1)) {
            raise_exception(ExcCode::CpU, 0, 1);
        } else {
            // FPU not implemented yet — treat as NOP but log.
            N64_TRACE("FPU load/store stub opcode={:02X}", op(insn));
        }
        break;

    default:
        ++unknown_opcode_count_;
        N64_WARN("Reserved instruction op={:02X} insn={:08X} @ {:08X}",
                 op(insn), insn, static_cast<u32>(pc_));
        raise_exception(ExcCode::RI);
        break;
    }
}

// =============================================================================
// SPECIAL
// =============================================================================

void Cpu::exec_special(u32 insn) {
    switch (fn(insn)) {
    case FN_SLL:
        write_gpr(rd(insn), sext32(rt32(insn) << sa(insn)));
        break;
    case FN_SRL:
        write_gpr(rd(insn), sext32(rt32(insn) >> sa(insn)));
        break;
    case FN_SRA:
        write_gpr(rd(insn), sext32(static_cast<u32>(rt32s(insn) >> sa(insn))));
        break;
    case FN_SLLV:
        write_gpr(rd(insn), sext32(rt32(insn) << (rs32(insn) & 31u)));
        break;
    case FN_SRLV:
        write_gpr(rd(insn), sext32(rt32(insn) >> (rs32(insn) & 31u)));
        break;
    case FN_SRAV:
        write_gpr(rd(insn), sext32(static_cast<u32>(rt32s(insn) >> (rs32(insn) & 31u))));
        break;

    case FN_JR:
        branch_abs(rs64(insn));
        break;
    case FN_JALR: {
        const u64 target = rs64(insn);
        const u32 d = rd(insn) == 0 ? 31u : rd(insn);
        link(d);
        branch_abs(target);
        break;
    }

    case FN_SYSCALL:
        do_syscall(insn);
        break;
    case FN_BREAK:
        do_break(insn);
        break;
    case FN_SYNC:
        break; // NOP in interpreter

    case FN_MFHI:
        write_gpr(rd(insn), hi_);
        break;
    case FN_MTHI:
        hi_ = rs64(insn);
        break;
    case FN_MFLO:
        write_gpr(rd(insn), lo_);
        break;
    case FN_MTLO:
        lo_ = rs64(insn);
        break;

    case FN_DSLLV:
        write_gpr(rd(insn), rt64(insn) << (rs32(insn) & 63u));
        break;
    case FN_DSRLV:
        write_gpr(rd(insn), rt64(insn) >> (rs32(insn) & 63u));
        break;
    case FN_DSRAV:
        write_gpr(rd(insn), static_cast<u64>(rt64s(insn) >> (rs32(insn) & 63u)));
        break;

    case FN_MULT: {
        const s64 r = static_cast<s64>(rs32s(insn)) * static_cast<s64>(rt32s(insn));
        lo_ = sext32(static_cast<u32>(static_cast<u64>(r)));
        hi_ = sext32(static_cast<u32>(static_cast<u64>(r) >> 32));
        break;
    }
    case FN_MULTU: {
        const u64 r = static_cast<u64>(rs32(insn)) * static_cast<u64>(rt32(insn));
        lo_ = sext32(static_cast<u32>(r));
        hi_ = sext32(static_cast<u32>(r >> 32));
        break;
    }
    case FN_DIV: {
        const s32 num = rs32s(insn);
        const s32 den = rt32s(insn);
        if (den == 0) {
            // INCÓGNITA: VR4300 divide-by-zero HI/LO values — leave unchanged-ish.
            // Common convention: LO = num < 0 ? 1 : -1; HI = num
            lo_ = sext32(num < 0 ? 1u : 0xFFFFFFFFu);
            hi_ = sext32(static_cast<u32>(num));
        } else if (num == static_cast<s32>(0x8000'0000u) && den == -1) {
            lo_ = sext32(0x8000'0000u);
            hi_ = 0;
        } else {
            lo_ = sext32(static_cast<u32>(num / den));
            hi_ = sext32(static_cast<u32>(num % den));
        }
        break;
    }
    case FN_DIVU: {
        const u32 num = rs32(insn);
        const u32 den = rt32(insn);
        if (den == 0) {
            lo_ = sext32(0xFFFFFFFFu);
            hi_ = sext32(num);
        } else {
            lo_ = sext32(num / den);
            hi_ = sext32(num % den);
        }
        break;
    }
    case FN_DMULT:
        mult_s64(rs64s(insn), rt64s(insn), hi_, lo_);
        break;
    case FN_DMULTU:
        mult_u64(rs64(insn), rt64(insn), hi_, lo_);
        break;
    case FN_DDIV: {
        const s64 num = rs64s(insn);
        const s64 den = rt64s(insn);
        if (den == 0) {
            lo_ = num < 0 ? 1ull : ~0ull;
            hi_ = static_cast<u64>(num);
        } else if (num == static_cast<s64>(0x8000'0000'0000'0000ull) && den == -1) {
            lo_ = static_cast<u64>(num);
            hi_ = 0;
        } else {
            lo_ = static_cast<u64>(num / den);
            hi_ = static_cast<u64>(num % den);
        }
        break;
    }
    case FN_DDIVU: {
        const u64 num = rs64(insn);
        const u64 den = rt64(insn);
        if (den == 0) {
            lo_ = ~0ull;
            hi_ = num;
        } else {
            lo_ = num / den;
            hi_ = num % den;
        }
        break;
    }

    case FN_ADD: {
        s32 result = 0;
        if (add_overflow_s32(rs32s(insn), rt32s(insn), result)) {
            raise_exception(ExcCode::Ov);
        } else {
            write_gpr(rd(insn), sext32(static_cast<u32>(result)));
        }
        break;
    }
    case FN_ADDU:
        write_gpr(rd(insn), sext32(rs32(insn) + rt32(insn)));
        break;
    case FN_SUB: {
        s32 result = 0;
        if (sub_overflow_s32(rs32s(insn), rt32s(insn), result)) {
            raise_exception(ExcCode::Ov);
        } else {
            write_gpr(rd(insn), sext32(static_cast<u32>(result)));
        }
        break;
    }
    case FN_SUBU:
        write_gpr(rd(insn), sext32(rs32(insn) - rt32(insn)));
        break;
    case FN_AND:
        write_gpr(rd(insn), rs64(insn) & rt64(insn));
        break;
    case FN_OR:
        write_gpr(rd(insn), rs64(insn) | rt64(insn));
        break;
    case FN_XOR:
        write_gpr(rd(insn), rs64(insn) ^ rt64(insn));
        break;
    case FN_NOR:
        write_gpr(rd(insn), ~(rs64(insn) | rt64(insn)));
        break;
    case FN_SLT:
        write_gpr(rd(insn), rs64s(insn) < rt64s(insn) ? 1ull : 0ull);
        break;
    case FN_SLTU:
        write_gpr(rd(insn), rs64(insn) < rt64(insn) ? 1ull : 0ull);
        break;

    case FN_DADD: {
        s64 result = 0;
        if (add_overflow_s64(rs64s(insn), rt64s(insn), result)) {
            raise_exception(ExcCode::Ov);
        } else {
            write_gpr(rd(insn), static_cast<u64>(result));
        }
        break;
    }
    case FN_DADDU:
        write_gpr(rd(insn), rs64(insn) + rt64(insn));
        break;
    case FN_DSUB: {
        s64 result = 0;
        if (sub_overflow_s64(rs64s(insn), rt64s(insn), result)) {
            raise_exception(ExcCode::Ov);
        } else {
            write_gpr(rd(insn), static_cast<u64>(result));
        }
        break;
    }
    case FN_DSUBU:
        write_gpr(rd(insn), rs64(insn) - rt64(insn));
        break;

    case FN_TGE:
        if (rs64s(insn) >= rt64s(insn)) raise_exception(ExcCode::Tr);
        break;
    case FN_TGEU:
        if (rs64(insn) >= rt64(insn)) raise_exception(ExcCode::Tr);
        break;
    case FN_TLT:
        if (rs64s(insn) < rt64s(insn)) raise_exception(ExcCode::Tr);
        break;
    case FN_TLTU:
        if (rs64(insn) < rt64(insn)) raise_exception(ExcCode::Tr);
        break;
    case FN_TEQ:
        if (rs64(insn) == rt64(insn)) raise_exception(ExcCode::Tr);
        break;
    case FN_TNE:
        if (rs64(insn) != rt64(insn)) raise_exception(ExcCode::Tr);
        break;

    case FN_DSLL:
        write_gpr(rd(insn), rt64(insn) << sa(insn));
        break;
    case FN_DSRL:
        write_gpr(rd(insn), rt64(insn) >> sa(insn));
        break;
    case FN_DSRA:
        write_gpr(rd(insn), static_cast<u64>(rt64s(insn) >> sa(insn)));
        break;
    case FN_DSLL32:
        write_gpr(rd(insn), rt64(insn) << (sa(insn) + 32u));
        break;
    case FN_DSRL32:
        write_gpr(rd(insn), rt64(insn) >> (sa(insn) + 32u));
        break;
    case FN_DSRA32:
        write_gpr(rd(insn), static_cast<u64>(rt64s(insn) >> (sa(insn) + 32u)));
        break;

    default:
        ++unknown_opcode_count_;
        N64_WARN("Unknown SPECIAL fn={:02X} insn={:08X} @ {:08X}",
                 fn(insn), insn, static_cast<u32>(pc_));
        raise_exception(ExcCode::RI);
        break;
    }
}

// =============================================================================
// REGIMM
// =============================================================================

void Cpu::exec_regimm(u32 insn) {
    const u32 rt_field = rt(insn);
    const bool rs_neg = rs64s(insn) < 0;
    const bool rs_ge0 = !rs_neg;

    switch (rt_field) {
    case RT_BLTZ:
        if (rs_neg) branch_rel(simm(insn));
        else branch_abs(pc_ + 8);
        break;
    case RT_BGEZ:
        if (rs_ge0) branch_rel(simm(insn));
        else branch_abs(pc_ + 8);
        break;
    case RT_BLTZL:
        if (rs_neg) branch_rel(simm(insn));
        else { next_pc_ = pc_ + 8; branch_pending_ = false; }
        break;
    case RT_BGEZL:
        if (rs_ge0) branch_rel(simm(insn));
        else { next_pc_ = pc_ + 8; branch_pending_ = false; }
        break;

    case RT_TGEI:
        if (rs64s(insn) >= static_cast<s64>(simm(insn))) raise_exception(ExcCode::Tr);
        break;
    case RT_TGEIU:
        if (rs64(insn) >= static_cast<u64>(static_cast<s64>(simm(insn)))) raise_exception(ExcCode::Tr);
        break;
    case RT_TLTI:
        if (rs64s(insn) < static_cast<s64>(simm(insn))) raise_exception(ExcCode::Tr);
        break;
    case RT_TLTIU:
        if (rs64(insn) < static_cast<u64>(static_cast<s64>(simm(insn)))) raise_exception(ExcCode::Tr);
        break;
    case RT_TEQI:
        if (rs64s(insn) == static_cast<s64>(simm(insn))) raise_exception(ExcCode::Tr);
        break;
    case RT_TNEI:
        if (rs64s(insn) != static_cast<s64>(simm(insn))) raise_exception(ExcCode::Tr);
        break;

    case RT_BLTZAL:
        link(31);
        if (rs_neg) branch_rel(simm(insn));
        else branch_abs(pc_ + 8);
        break;
    case RT_BGEZAL:
        link(31);
        if (rs_ge0) branch_rel(simm(insn));
        else branch_abs(pc_ + 8);
        break;
    case RT_BLTZALL:
        link(31);
        if (rs_neg) branch_rel(simm(insn));
        else { next_pc_ = pc_ + 8; branch_pending_ = false; }
        break;
    case RT_BGEZALL:
        link(31);
        if (rs_ge0) branch_rel(simm(insn));
        else { next_pc_ = pc_ + 8; branch_pending_ = false; }
        break;

    default:
        ++unknown_opcode_count_;
        N64_WARN("Unknown REGIMM rt={:02X} @ {:08X}", rt_field, static_cast<u32>(pc_));
        raise_exception(ExcCode::RI);
        break;
    }
}

// =============================================================================
// COP0
// =============================================================================

void Cpu::exec_cop0(u32 insn) {
    if (!coprocessor_usable(0)) {
        raise_exception(ExcCode::CpU, 0, 0);
        return;
    }

    const u32 co_rs = rs(insn);
    switch (co_rs) {
    case 0x00: { // MFC0
        const u32 val = cop0_[rd(insn)];
        write_gpr(rt(insn), sext32(val));
        break;
    }
    case 0x01: { // DMFC0
        // VR4300 COP0 regs are 32-bit architecturally for most; expose zero-ext.
        write_gpr(rt(insn), static_cast<u64>(cop0_[rd(insn)]));
        break;
    }
    case 0x04: { // MTC0
        set_cop0(rd(insn), rt32(insn));
        break;
    }
    case 0x05: { // DMTC0
        set_cop0(rd(insn), static_cast<u32>(rt64(insn)));
        break;
    }
    case 0x10: { // CO (bit 25 set form uses fn)
        // When rs bit4 (co) is set, rs field has bit 0x10.
        const u32 c0fn = fn(insn);
        switch (c0fn) {
        case 0x01: // TLBR
        case 0x02: // TLBWI
        case 0x06: // TLBWR
        case 0x08: // TLBP
            // TLB ops: no-op until TLB implemented (Phase 2/10).
            N64_TRACE("COP0 TLB op {:02X} stub", c0fn);
            break;
        case 0x18: // ERET
            do_eret();
            break;
        default:
            N64_WARN("Unknown COP0 CO fn={:02X}", c0fn);
            break;
        }
        break;
    }
    default:
        // Also handle co-format when insn bit 25 is set (rs >= 16)
        if (insn & (1u << 25)) {
            if (fn(insn) == 0x18) {
                do_eret();
            } else {
                N64_TRACE("COP0 CO fn={:02X} stub", fn(insn));
            }
        } else {
            N64_WARN("Unknown COP0 rs={:02X} insn={:08X}", co_rs, insn);
        }
        break;
    }
}

void Cpu::exec_cop1(u32 insn) {
    if (!coprocessor_usable(1)) {
        raise_exception(ExcCode::CpU, 0, 1);
        return;
    }
    // FPU not in Phase 1 scope — NOP with count.
    N64_TRACE("COP1 stub insn={:08X}", insn);
    (void)insn;
}

void Cpu::exec_cop2(u32 insn) {
    if (!coprocessor_usable(2)) {
        raise_exception(ExcCode::CpU, 0, 2);
        return;
    }
    N64_TRACE("COP2 stub insn={:08X}", insn);
    (void)insn;
}

void Cpu::do_syscall(u32 /*insn*/) {
    raise_exception(ExcCode::Sys);
}

void Cpu::do_break(u32 /*insn*/) {
    raise_exception(ExcCode::Bp);
}

void Cpu::do_eret() {
    u32 sr = cop0_[Cop0Reg::Status];
    u64 target;
    if (sr & SR_ERL) {
        sr &= ~SR_ERL;
        target = cop0_[Cop0Reg::ErrorEPC];
    } else {
        sr &= ~SR_EXL;
        target = cop0_[Cop0Reg::EPC];
    }
    cop0_[Cop0Reg::Status] = sr;
    ll_bit_ = false;

    // ERET has no delay slot — jump immediately.
    pc_ = target;
    next_pc_ = target + 4;
    branch_pending_ = false;

    // Signal step() not to apply normal PC advance: we do this by bumping
    // exception_count_? Better: use a dedicated path.
    // We'll set a flag via branch_pending_ false and pc_ already at target.
    // step() after execute does pc_=next_pc_ if sequential — that would skip!
    // Fix: set next_pc_ = target so sequential advance lands on target...
    // Actually after execute, step does:
    //   if (!delay && !branch_pending_) pc_ = next_pc_
    // If we set pc_=target and next_pc_=target, then pc_ becomes target (ok).
    // Then next_pc_ = pc_+4 = target+4. Good.
    // So set both to target; sequential path: pc_ = next_pc_ = target. Correct.
    pc_ = target;
    next_pc_ = target;
    branch_pending_ = false;
}

// =============================================================================
// Disassembler (compact, for traces)
// =============================================================================

std::string Cpu::disassemble(u32 insn) const {
    char buf[64];
    const u32 o = op(insn);
    if (o == OP_SPECIAL) {
        std::snprintf(buf, sizeof(buf), "SPECIAL fn=%02X rd=%u rs=%u rt=%u sa=%u",
                      fn(insn), rd(insn), rs(insn), rt(insn), sa(insn));
    } else if (o == OP_REGIMM) {
        std::snprintf(buf, sizeof(buf), "REGIMM rt=%02X rs=%u imm=%d",
                      rt(insn), rs(insn), static_cast<int>(simm(insn)));
    } else if (o == OP_J || o == OP_JAL) {
        std::snprintf(buf, sizeof(buf), "%s 0x%08X",
                      o == OP_J ? "J" : "JAL", target(insn) << 2);
    } else if (o == OP_COP0) {
        std::snprintf(buf, sizeof(buf), "COP0 rs=%02X rt=%u rd=%u",
                      rs(insn), rt(insn), rd(insn));
    } else {
        std::snprintf(buf, sizeof(buf), "op=%02X rs=%u rt=%u imm=0x%04X",
                      o, rs(insn), rt(insn), imm(insn));
    }
    return std::string(buf);
}

} // namespace n64
