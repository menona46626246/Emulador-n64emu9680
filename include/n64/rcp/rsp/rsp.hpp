#pragma once

#include "n64/common/types.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace n64 {

class Bus;
class SpRegisters;
class MipsInterface;

/// Reality Signal Processor scalar and COP2 vector interpreter.
class Rsp {
public:
    static constexpr std::size_t kGprCount = 32;
    static constexpr std::size_t kVectorRegisterCount = 32;
    static constexpr std::size_t kVectorLaneCount = 8;
    static constexpr u32 kImemMask = 0xFFCu;

    using BreakCallback = std::function<void(bool intr_on_break)>;

    void reset();
    void connect(Bus* bus, SpRegisters* sp, MipsInterface* mi) noexcept;

    [[nodiscard]] std::span<u8> dmem() noexcept { return dmem_; }
    [[nodiscard]] std::span<const u8> dmem() const noexcept { return dmem_; }
    [[nodiscard]] std::span<u8> imem() noexcept { return imem_; }
    [[nodiscard]] std::span<const u8> imem() const noexcept { return imem_; }

    [[nodiscard]] bool halted() const noexcept { return halted_; }
    [[nodiscard]] bool broke() const noexcept { return broke_; }
    void set_halted(bool h) noexcept;
    void set_broke(bool b) noexcept { broke_ = b; }
    void set_intr_on_break(bool v) noexcept { intr_on_break_ = v; }
    [[nodiscard]] bool intr_on_break() const noexcept { return intr_on_break_; }

    [[nodiscard]] u32 pc() const noexcept { return pc_; }
    void set_pc(u32 pc) noexcept {
        pc_ = pc & kImemMask;
        next_pc_ = (pc_ + 4) & kImemMask;
        branch_pending_ = false;
    }

    [[nodiscard]] u32 gpr(std::size_t i) const noexcept { return gpr_[i & 31]; }
    void set_gpr(std::size_t i, u32 v) noexcept {
        if ((i & 31) != 0) gpr_[i & 31] = v;
    }

    [[nodiscard]] u16 vpr(std::size_t reg, std::size_t lane) const noexcept {
        return vpr_[reg & 31][lane & 7];
    }
    void set_vpr(std::size_t reg, std::size_t lane, u16 value) noexcept {
        vpr_[reg & 31][lane & 7] = value;
    }
    [[nodiscard]] s64 accumulator(std::size_t lane) const noexcept {
        return accumulator_[lane & 7];
    }
    void set_accumulator(std::size_t lane, s64 value) noexcept;
    [[nodiscard]] u16 vco() const noexcept { return vco_; }
    [[nodiscard]] u16 vcc() const noexcept { return vcc_; }
    [[nodiscard]] u8 vce() const noexcept { return vce_; }
    void set_vco(u16 value) noexcept { vco_ = value; }
    void set_vcc(u16 value) noexcept { vcc_ = value; }
    void set_vce(u8 value) noexcept { vce_ = value; }

    [[nodiscard]] u64 cycles() const noexcept { return cycles_; }
    [[nodiscard]] u64 instructions_retired() const noexcept { return retired_; }

    /// Single scalar step. No-op if halted.
    void step();

    /// Run up to `max_steps` or until halted/broke. Returns steps taken.
    u32 run(u32 max_steps);

    /// Sync internal DMEM/IMEM from bus windows (after external DMA into bus).
    void pull_mem_from_bus();
    /// Push internal DMEM/IMEM out to bus windows.
    void push_mem_to_bus();

    void set_break_callback(BreakCallback cb) { on_break_ = std::move(cb); }

private:
    static constexpr u32 op(u32 i) noexcept { return i >> 26; }
    static constexpr u32 rs(u32 i) noexcept { return (i >> 21) & 31; }
    static constexpr u32 rt(u32 i) noexcept { return (i >> 16) & 31; }
    static constexpr u32 rd(u32 i) noexcept { return (i >> 11) & 31; }
    static constexpr u32 sa(u32 i) noexcept { return (i >> 6) & 31; }
    static constexpr u32 fn(u32 i) noexcept { return i & 63; }
    static constexpr u32 imm(u32 i) noexcept { return i & 0xFFFFu; }
    static constexpr s16 simm(u32 i) noexcept { return static_cast<s16>(i & 0xFFFFu); }
    static constexpr u32 target(u32 i) noexcept { return i & 0x03FFFFFF; }

    void write_gpr(u32 idx, u32 value) noexcept {
        if (idx != 0) gpr_[idx] = value;
    }

    [[nodiscard]] u32 fetch(u32 pc) const;
    [[nodiscard]] u32 load_byte(u32 addr) const;
    [[nodiscard]] u32 load_half(u32 addr) const;
    [[nodiscard]] u32 load_word(u32 addr) const;
    void store_byte(u32 addr, u8 v);
    void store_half(u32 addr, u16 v);
    void store_word(u32 addr, u32 v);

    void branch_abs(u32 target);
    void branch_rel(s32 off_imm);
    void link(u32 reg);

    void execute(u32 insn);
    void exec_special(u32 insn);
    void exec_regimm(u32 insn);
    void exec_cop0(u32 insn);
    void exec_cop2(u32 insn);
    void exec_vector(u32 insn);
    void exec_vector_memory(u32 insn, bool store);
    void do_break(u32 insn);

    [[nodiscard]] u8 vector_byte(u32 reg, u32 byte) const noexcept;
    void set_vector_byte(u32 reg, u32 byte, u8 value) noexcept;

    // Unaligned LWL/LWR/SWL/SWR (big-endian, like VR4300)
    void exec_lwl(u32 insn);
    void exec_lwr(u32 insn);
    void exec_swl(u32 insn);
    void exec_swr(u32 insn);

    std::array<u8, kSpDmemSize> dmem_{};
    std::array<u8, kSpImemSize> imem_{};
    std::array<u32, kGprCount> gpr_{};
    std::array<std::array<u16, kVectorLaneCount>, kVectorRegisterCount> vpr_{};
    std::array<s64, kVectorLaneCount> accumulator_{};
    u16 vco_ = 0;
    u16 vcc_ = 0;
    u8 vce_ = 0;
    s32 div_in_ = 0;
    s32 div_out_ = 0;
    bool div_high_pending_ = false;

    u32 pc_ = 0;
    u32 next_pc_ = 4;
    bool branch_pending_ = false;
    bool in_delay_slot_ = false;
    bool halted_ = true;
    bool broke_ = false;
    bool intr_on_break_ = false;
    u64 cycles_ = 0;
    u64 retired_ = 0;

    Bus* bus_ = nullptr;
    SpRegisters* sp_ = nullptr;
    MipsInterface* mi_ = nullptr;
    BreakCallback on_break_;
};

} // namespace n64
