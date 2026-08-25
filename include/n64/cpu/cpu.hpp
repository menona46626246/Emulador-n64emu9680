#pragma once

#include "n64/common/types.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace n64 {

class Bus;
class BlockCache;
struct BasicBlock;

/// COP0 register indices (VR4300 / MIPS III).
namespace Cop0Reg {
    constexpr u32 Index    = 0;
    constexpr u32 Random   = 1;
    constexpr u32 EntryLo0 = 2;
    constexpr u32 EntryLo1 = 3;
    constexpr u32 Context  = 4;
    constexpr u32 PageMask = 5;
    constexpr u32 Wired    = 6;
    constexpr u32 BadVAddr = 8;
    constexpr u32 Count    = 9;
    constexpr u32 EntryHi  = 10;
    constexpr u32 Compare  = 11;
    constexpr u32 Status   = 12;
    constexpr u32 Cause    = 13;
    constexpr u32 EPC      = 14;
    constexpr u32 PRId     = 15;
    constexpr u32 Config   = 16;
    constexpr u32 LLAddr   = 17;
    constexpr u32 WatchLo  = 18;
    constexpr u32 WatchHi  = 19;
    constexpr u32 XContext = 20;
    constexpr u32 PErr     = 26;
    constexpr u32 CacheErr = 27;
    constexpr u32 TagLo    = 28;
    constexpr u32 TagHi    = 29;
    constexpr u32 ErrorEPC = 30;
}

/// Exception codes written to Cause.ExcCode (bits 6:2).
namespace ExcCode {
    constexpr u32 Int    = 0;  // Interrupt
    constexpr u32 Mod    = 1;  // TLB modification
    constexpr u32 TLBL   = 2;  // TLB miss (load/fetch)
    constexpr u32 TLBS   = 3;  // TLB miss (store)
    constexpr u32 AdEL   = 4;  // Address error (load/fetch)
    constexpr u32 AdES   = 5;  // Address error (store)
    constexpr u32 IBE    = 6;  // Bus error (instruction)
    constexpr u32 DBE    = 7;  // Bus error (data)
    constexpr u32 Sys    = 8;  // Syscall
    constexpr u32 Bp     = 9;  // Breakpoint
    constexpr u32 RI     = 10; // Reserved instruction
    constexpr u32 CpU    = 11; // Coprocessor unusable
    constexpr u32 Ov     = 12; // Arithmetic overflow
    constexpr u32 Tr     = 13; // Trap
}

/// VR4300 (MIPS III) interpreter.
/// Phase 1: full base ISA + COP0 + exceptions + delay slots. No TLB walk yet
/// (KSEG0/1 direct-map; KUSEG identity-maps low 512 MiB for synthetic tests).
class Cpu {
public:
    static constexpr std::size_t kGprCount = 32;
    static constexpr std::size_t kCop0Count = 32;

    /// Optional per-instruction trace callback (pc, insn, disasm).
    using TraceFn = std::function<void(u64 pc, u32 insn, const std::string& disasm)>;

    Cpu() = default;

    void reset();
    void connect_bus(Bus* bus) noexcept { bus_ = bus; }

    /// Execute one instruction (handles branch delay slots).
    void step();

    /// Execute up to `n` instructions or until halted. Returns steps taken.
    Cycles run(Cycles n);

    // ----- GPR ---------------------------------------------------------------
    [[nodiscard]] u64 gpr(std::size_t i) const noexcept { return gpr_[i & 31]; }
    void set_gpr(std::size_t i, u64 value) noexcept {
        if ((i & 31) != 0) {
            gpr_[i & 31] = value;
        }
    }

    // ----- PC ----------------------------------------------------------------
    [[nodiscard]] u64 pc() const noexcept { return pc_; }
    /// Set PC and clear any pending branch (next_pc = pc+4).
    void set_pc(u64 pc) noexcept {
        pc_ = pc;
        next_pc_ = pc + 4;
        branch_pending_ = false;
        ll_bit_ = false;
    }

    [[nodiscard]] u64 next_pc() const noexcept { return next_pc_; }

    // ----- HI/LO -------------------------------------------------------------
    [[nodiscard]] u64 hi() const noexcept { return hi_; }
    [[nodiscard]] u64 lo() const noexcept { return lo_; }
    void set_hi(u64 v) noexcept { hi_ = v; }
    void set_lo(u64 v) noexcept { lo_ = v; }

    // ----- COP0 --------------------------------------------------------------
    [[nodiscard]] u32 cop0(std::size_t i) const noexcept { return cop0_[i & 31]; }
    void set_cop0(std::size_t i, u32 v) noexcept;

    // ----- State -------------------------------------------------------------
    [[nodiscard]] Cycles cycles() const noexcept { return cycles_; }
    [[nodiscard]] bool halted() const noexcept { return halted_; }
    void set_halted(bool h) noexcept { halted_ = h; }

    [[nodiscard]] bool in_delay_slot() const noexcept { return in_delay_slot_; }
    [[nodiscard]] u64 exception_count() const noexcept { return exception_count_; }
    [[nodiscard]] u64 unknown_opcode_count() const noexcept { return unknown_opcode_count_; }

    [[nodiscard]] Bus* bus() const noexcept { return bus_; }

    void set_trace(TraceFn fn) { trace_ = std::move(fn); }
    void clear_trace() { trace_ = nullptr; }

    /// Translate a virtual address for testing / tooling.
    /// Returns false if the access would raise an address/TLB exception.
    [[nodiscard]] bool translate(u64 vaddr, bool is_store, PhysicalAddress& out_paddr) const;

    /// Raise a CPU exception (public for tests).
    void raise_exception(u32 exc_code, u64 bad_vaddr = 0, u32 ce = 0);

    /// Set/clear the RCP external interrupt line (COP0 Cause IP2).
    void set_rcp_interrupt(bool level) noexcept;

    /// Check pending interrupts (Status.IE, !EXL, !ERL, IM & IP). May raise Int.
    void check_interrupts();

    // ----- Block Cache ------------------------------------------------------
    void set_block_cache(BlockCache* cache) noexcept { block_cache_ = cache; }
    [[nodiscard]] BlockCache* block_cache() const noexcept { return block_cache_; }
    void set_block_cache_enabled(bool enabled) noexcept { block_cache_enabled_ = enabled; }
    [[nodiscard]] bool block_cache_enabled() const noexcept { return block_cache_enabled_; }

    /// Execute a basic block. Returns number of instructions executed.
    Cycles execute_block(const BasicBlock& block);

private:
    // Instruction field helpers
    static constexpr u32 op(u32 i)    noexcept { return i >> 26; }
    static constexpr u32 rs(u32 i)    noexcept { return (i >> 21) & 31; }
    static constexpr u32 rt(u32 i)    noexcept { return (i >> 16) & 31; }
    static constexpr u32 rd(u32 i)    noexcept { return (i >> 11) & 31; }
    static constexpr u32 sa(u32 i)    noexcept { return (i >> 6) & 31; }
    static constexpr u32 fn(u32 i)    noexcept { return i & 63; }
    static constexpr u32 imm(u32 i)   noexcept { return i & 0xFFFFu; }
    static constexpr s16 simm(u32 i)  noexcept { return static_cast<s16>(i & 0xFFFFu); }
    static constexpr u32 target(u32 i) noexcept { return i & 0x03FFFFFF; }

    [[nodiscard]] u64 rs64(u32 i) const noexcept { return gpr_[rs(i)]; }
    [[nodiscard]] u64 rt64(u32 i) const noexcept { return gpr_[rt(i)]; }
    [[nodiscard]] s64 rs64s(u32 i) const noexcept { return static_cast<s64>(gpr_[rs(i)]); }
    [[nodiscard]] s64 rt64s(u32 i) const noexcept { return static_cast<s64>(gpr_[rt(i)]); }
    [[nodiscard]] u32 rs32(u32 i) const noexcept { return static_cast<u32>(gpr_[rs(i)]); }
    [[nodiscard]] u32 rt32(u32 i) const noexcept { return static_cast<u32>(gpr_[rt(i)]); }
    [[nodiscard]] s32 rs32s(u32 i) const noexcept { return static_cast<s32>(static_cast<u32>(gpr_[rs(i)])); }
    [[nodiscard]] s32 rt32s(u32 i) const noexcept { return static_cast<s32>(static_cast<u32>(gpr_[rt(i)])); }

    void write_gpr(u32 idx, u64 value) noexcept {
        if (idx != 0) {
            gpr_[idx] = value;
        }
    }

    /// Sign-extend 32-bit result into 64-bit GPR (MIPS III load/ALU convention).
    static constexpr u64 sext32(u32 v) noexcept {
        return static_cast<u64>(static_cast<s64>(static_cast<s32>(v)));
    }
    static constexpr u64 sext16(u16 v) noexcept {
        return static_cast<u64>(static_cast<s64>(static_cast<s16>(v)));
    }
    static constexpr u64 sext8(u8 v) noexcept {
        return static_cast<u64>(static_cast<s64>(static_cast<s8>(v)));
    }

    [[nodiscard]] u32 fetch(u64 vaddr);
    [[nodiscard]] bool probe_read(u64 vaddr, int size, PhysicalAddress& paddr);
    [[nodiscard]] bool probe_write(u64 vaddr, int size, PhysicalAddress& paddr);

    [[nodiscard]] bool load_byte(u64 vaddr, bool sign_extend, u64& value);
    [[nodiscard]] bool load_half(u64 vaddr, bool sign_extend, u64& value);
    [[nodiscard]] bool load_word(u64 vaddr, bool sign_extend, u64& value);
    [[nodiscard]] bool load_double(u64 vaddr, u64& value);
    void store_byte(u64 vaddr, u8 value);
    void store_half(u64 vaddr, u16 value);
    void store_word(u64 vaddr, u32 value);
    void store_double(u64 vaddr, u64 value);

    // Unaligned load/store helpers (LWL/LWR/SWL/SWR/LDL/LDR/SDL/SDR)
    void exec_lwl(u32 insn);
    void exec_lwr(u32 insn);
    void exec_swl(u32 insn);
    void exec_swr(u32 insn);
    void exec_ldl(u32 insn);
    void exec_ldr(u32 insn);
    void exec_sdl(u32 insn);
    void exec_sdr(u32 insn);

    void branch_abs(u64 target_pc);
    void branch_rel(s32 offset_imm);
    void link(u32 reg);

    void execute(u32 insn);
    void exec_special(u32 insn);
    void exec_regimm(u32 insn);
    void exec_cop0(u32 insn);
    void exec_cop1(u32 insn);
    void exec_cop2(u32 insn);

    void do_syscall(u32 insn);
    void do_break(u32 insn);
    void do_eret();

    [[nodiscard]] std::string disassemble(u32 insn) const;

    // Status helpers
    [[nodiscard]] bool coprocessor_usable(u32 cop) const noexcept;
    [[nodiscard]] bool kernel_mode() const noexcept;
    void advance_count();

    std::array<u64, kGprCount> gpr_{};
    std::array<u32, kCop0Count> cop0_{};

    u64 pc_ = 0;
    u64 next_pc_ = 0;
    u64 hi_ = 0;
    u64 lo_ = 0;

    Cycles cycles_ = 0;
    bool halted_ = false;
    bool in_delay_slot_ = false;
    bool branch_pending_ = false; // next_pc_ is a branch target after delay slot
    bool ll_bit_ = false;         // Load-linked bit
    u64 ll_addr_ = 0;

    u64 exception_count_ = 0;
    u64 unknown_opcode_count_ = 0;

    Bus* bus_ = nullptr;
    TraceFn trace_;
    BlockCache* block_cache_ = nullptr;
    bool block_cache_enabled_ = true;
};

} // namespace n64
