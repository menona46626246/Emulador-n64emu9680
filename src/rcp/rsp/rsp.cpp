#include "n64/rcp/rsp/rsp.hpp"

#include "n64/bus/bus.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/bus/sp_regs.hpp"
#include "n64/common/log.hpp"

#include <algorithm>
#include <cstring>

namespace n64 {
namespace {

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
constexpr u32 OP_COP2    = 0x12;
constexpr u32 OP_LB      = 0x20;
constexpr u32 OP_LH      = 0x21;
constexpr u32 OP_LWL     = 0x22;
constexpr u32 OP_LW      = 0x23;
constexpr u32 OP_LBU     = 0x24;
constexpr u32 OP_LHU     = 0x25;
constexpr u32 OP_LWR     = 0x26;
constexpr u32 OP_SB      = 0x28;
constexpr u32 OP_SH      = 0x29;
constexpr u32 OP_SWL     = 0x2A;
constexpr u32 OP_SW      = 0x2B;
constexpr u32 OP_SWR     = 0x2E;

// Vector / COP2 memory ops
constexpr u32 OP_LWC2    = 0x32;
constexpr u32 OP_SWC2    = 0x3A;

constexpr u32 FN_SLL     = 0x00;
constexpr u32 FN_SRL     = 0x02;
constexpr u32 FN_SRA     = 0x03;
constexpr u32 FN_SLLV    = 0x04;
constexpr u32 FN_SRLV    = 0x06;
constexpr u32 FN_SRAV    = 0x07;
constexpr u32 FN_JR      = 0x08;
constexpr u32 FN_JALR    = 0x09;
constexpr u32 FN_BREAK   = 0x0D;
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

constexpr u32 RT_BLTZ    = 0x00;
constexpr u32 RT_BGEZ    = 0x01;
constexpr u32 RT_BLTZAL  = 0x10;
constexpr u32 RT_BGEZAL  = 0x11;

} // namespace

void Rsp::connect(Bus* bus, SpRegisters* sp, MipsInterface* mi) noexcept {
    bus_ = bus;
    sp_ = sp;
    mi_ = mi;
}

void Rsp::reset() {
    dmem_.fill(0);
    imem_.fill(0);
    gpr_.fill(0);
    for (auto& reg : vpr_) {
        reg.fill(0);
    }
    accumulator_.fill(0);
    vco_ = 0;
    vcc_ = 0;
    vce_ = 0;
    div_in_ = 0;
    div_out_ = 0;
    div_high_pending_ = false;
    pc_ = 0;
    next_pc_ = 4;
    branch_pending_ = false;
    in_delay_slot_ = false;
    halted_ = true;
    broke_ = false;
    intr_on_break_ = false;
    cycles_ = 0;
    retired_ = 0;
    N64_DEBUG("RSP reset (halted)");
}

void Rsp::set_halted(bool h) noexcept {
    halted_ = h;
    if (!h) {
        broke_ = false;
    }
}

void Rsp::pull_mem_from_bus() {
    if (!bus_) return;
    auto bd = bus_->sp_dmem();
    auto bi = bus_->sp_imem();
    std::copy(bd.begin(), bd.end(), dmem_.begin());
    std::copy(bi.begin(), bi.end(), imem_.begin());
}

void Rsp::push_mem_to_bus() {
    if (!bus_) return;
    auto bd = bus_->sp_dmem();
    auto bi = bus_->sp_imem();
    std::copy(dmem_.begin(), dmem_.end(), bd.begin());
    std::copy(imem_.begin(), imem_.end(), bi.begin());
}

u32 Rsp::fetch(u32 pc) const {
    const u32 off = pc & kImemMask;
    return (static_cast<u32>(imem_[off]) << 24) |
           (static_cast<u32>(imem_[off + 1]) << 16) |
           (static_cast<u32>(imem_[off + 2]) << 8) |
           (static_cast<u32>(imem_[off + 3]));
}

u32 Rsp::load_byte(u32 addr) const {
    return dmem_[addr & 0xFFFu];
}

u32 Rsp::load_half(u32 addr) const {
    const u32 a = addr & 0xFFFu;
    return (static_cast<u32>(dmem_[a]) << 8) | dmem_[(a + 1) & 0xFFFu];
}

u32 Rsp::load_word(u32 addr) const {
    const u32 a = addr & 0xFFFu;
    return (static_cast<u32>(dmem_[a]) << 24) |
           (static_cast<u32>(dmem_[(a + 1) & 0xFFFu]) << 16) |
           (static_cast<u32>(dmem_[(a + 2) & 0xFFFu]) << 8) |
           (static_cast<u32>(dmem_[(a + 3) & 0xFFFu]));
}

void Rsp::store_byte(u32 addr, u8 v) {
    const u32 a = addr & 0xFFFu;
    dmem_[a] = v;
    if (bus_) {
        bus_->sp_dmem()[a] = v;
        bus_->notify_memory_write(0x0400'0000u + a, 1);
    }
}

void Rsp::store_half(u32 addr, u16 v) {
    const u32 a = addr & 0xFFFu;
    dmem_[a] = static_cast<u8>((v >> 8) & 0xFF);
    dmem_[(a + 1) & 0xFFFu] = static_cast<u8>(v & 0xFF);
    if (bus_) {
        auto bus_dmem = bus_->sp_dmem();
        bus_dmem[a] = dmem_[a];
        bus_dmem[(a + 1) & 0xFFFu] = dmem_[(a + 1) & 0xFFFu];
        bus_->notify_memory_write(0x0400'0000u + a, 1);
        bus_->notify_memory_write(0x0400'0000u + ((a + 1) & 0xFFFu), 1);
    }
}

void Rsp::store_word(u32 addr, u32 v) {
    const u32 a = addr & 0xFFFu;
    dmem_[a]                   = static_cast<u8>((v >> 24) & 0xFF);
    dmem_[(a + 1) & 0xFFFu]    = static_cast<u8>((v >> 16) & 0xFF);
    dmem_[(a + 2) & 0xFFFu]    = static_cast<u8>((v >> 8) & 0xFF);
    dmem_[(a + 3) & 0xFFFu]    = static_cast<u8>(v & 0xFF);
    if (bus_) {
        auto bus_dmem = bus_->sp_dmem();
        for (u32 i = 0; i < 4; ++i) {
            bus_dmem[(a + i) & 0xFFFu] = dmem_[(a + i) & 0xFFFu];
            bus_->notify_memory_write(0x0400'0000u + ((a + i) & 0xFFFu), 1);
        }
    }
}

void Rsp::branch_abs(u32 target) {
    next_pc_ = target & kImemMask;
    branch_pending_ = true;
}

void Rsp::branch_rel(s32 off_imm) {
    const s32 off = off_imm << 2;
    next_pc_ = static_cast<u32>(static_cast<s32>(pc_ + 4) + off) & kImemMask;
    branch_pending_ = true;
}

void Rsp::link(u32 reg) {
    write_gpr(reg, (pc_ + 8) & kImemMask);
}

u32 Rsp::run(u32 max_steps) {
    u32 n = 0;
    while (n < max_steps && !halted_) {
        step();
        ++n;
    }
    return n;
}

void Rsp::step() {
    if (halted_) {
        ++cycles_;
        return;
    }

    const u32 insn_pc = pc_;
    const bool delay_slot = branch_pending_;
    in_delay_slot_ = delay_slot;

    const u32 insn = fetch(insn_pc);

    if (!delay_slot) {
        next_pc_ = (insn_pc + 4) & kImemMask;
        branch_pending_ = false;
    }

    execute(insn);

    // BREAK may have halted us mid-execute.
    if (halted_) {
        gpr_[0] = 0;
        ++cycles_;
        ++retired_;
        in_delay_slot_ = false;
        push_mem_to_bus();
        return;
    }

    if (delay_slot) {
        pc_ = next_pc_;
        next_pc_ = (pc_ + 4) & kImemMask;
        branch_pending_ = false;
    } else if (branch_pending_) {
        const u32 target = next_pc_;
        pc_ = (insn_pc + 4) & kImemMask; // delay slot
        next_pc_ = target;
    } else {
        pc_ = next_pc_;
        next_pc_ = (pc_ + 4) & kImemMask;
    }

    gpr_[0] = 0;
    ++cycles_;
    ++retired_;
    in_delay_slot_ = false;
}

void Rsp::do_break(u32 /*insn*/) {
    broke_ = true;
    halted_ = true;
    branch_pending_ = false;
    push_mem_to_bus();
    N64_DEBUG("RSP BREAK at PC={:03X} intr_on_break={}", pc_, intr_on_break_);
    if (sp_) {
        sp_->notify_break(intr_on_break_);
    } else if (intr_on_break_ && mi_) {
        mi_->raise(mmio::MiIntr::SP);
    }
    if (on_break_) {
        on_break_(intr_on_break_);
    }
}

void Rsp::execute(u32 insn) {
    switch (op(insn)) {
    case OP_SPECIAL: exec_special(insn); break;
    case OP_REGIMM:  exec_regimm(insn);  break;
    case OP_J: {
        branch_abs((target(insn) << 2) & kImemMask);
        break;
    }
    case OP_JAL: {
        link(31);
        branch_abs((target(insn) << 2) & kImemMask);
        break;
    }
    case OP_BEQ:
        if (gpr_[rs(insn)] == gpr_[rt(insn)]) branch_rel(simm(insn));
        break;
    case OP_BNE:
        if (gpr_[rs(insn)] != gpr_[rt(insn)]) branch_rel(simm(insn));
        break;
    case OP_BLEZ:
        if (static_cast<s32>(gpr_[rs(insn)]) <= 0) branch_rel(simm(insn));
        break;
    case OP_BGTZ:
        if (static_cast<s32>(gpr_[rs(insn)]) > 0) branch_rel(simm(insn));
        break;
    case OP_ADDI:
    case OP_ADDIU:
        write_gpr(rt(insn), gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn))));
        break;
    case OP_SLTI:
        write_gpr(rt(insn),
                  static_cast<s32>(gpr_[rs(insn)]) < static_cast<s32>(simm(insn)) ? 1u : 0u);
        break;
    case OP_SLTIU:
        write_gpr(rt(insn),
                  gpr_[rs(insn)] < static_cast<u32>(static_cast<s32>(simm(insn))) ? 1u : 0u);
        break;
    case OP_ANDI:
        write_gpr(rt(insn), gpr_[rs(insn)] & imm(insn));
        break;
    case OP_ORI:
        write_gpr(rt(insn), gpr_[rs(insn)] | imm(insn));
        break;
    case OP_XORI:
        write_gpr(rt(insn), gpr_[rs(insn)] ^ imm(insn));
        break;
    case OP_LUI:
        write_gpr(rt(insn), imm(insn) << 16);
        break;
    case OP_COP0: exec_cop0(insn); break;
    case OP_COP2: exec_cop2(insn); break;

    case OP_LB:
        write_gpr(rt(insn),
                  static_cast<u32>(static_cast<s32>(static_cast<s8>(
                      load_byte(gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn))))))));
        break;
    case OP_LH:
        write_gpr(rt(insn),
                  static_cast<u32>(static_cast<s32>(static_cast<s16>(
                      load_half(gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn))))))));
        break;
    case OP_LWL: exec_lwl(insn); break;
    case OP_LW:
        write_gpr(rt(insn),
                  load_word(gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn)))));
        break;
    case OP_LBU:
        write_gpr(rt(insn),
                  load_byte(gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn)))));
        break;
    case OP_LHU:
        write_gpr(rt(insn),
                  load_half(gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn)))));
        break;
    case OP_LWR: exec_lwr(insn); break;
    case OP_SB:
        store_byte(gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn))),
                   static_cast<u8>(gpr_[rt(insn)]));
        break;
    case OP_SH:
        store_half(gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn))),
                   static_cast<u16>(gpr_[rt(insn)]));
        break;
    case OP_SWL: exec_swl(insn); break;
    case OP_SW:
        store_word(gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn))),
                   gpr_[rt(insn)]);
        break;
    case OP_SWR: exec_swr(insn); break;

    case OP_LWC2: exec_vector_memory(insn, false); break;
    case OP_SWC2: exec_vector_memory(insn, true);  break;

    default:
        N64_WARN("RSP reserved op={:02X} insn={:08X} @ {:03X}",
                 op(insn), insn, pc_);
        break;
    }
}

void Rsp::exec_special(u32 insn) {
    switch (fn(insn)) {
    case FN_SLL:
        write_gpr(rd(insn), gpr_[rt(insn)] << sa(insn));
        break;
    case FN_SRL:
        write_gpr(rd(insn), gpr_[rt(insn)] >> sa(insn));
        break;
    case FN_SRA:
        write_gpr(rd(insn),
                  static_cast<u32>(static_cast<s32>(gpr_[rt(insn)]) >> sa(insn)));
        break;
    case FN_SLLV:
        write_gpr(rd(insn), gpr_[rt(insn)] << (gpr_[rs(insn)] & 31u));
        break;
    case FN_SRLV:
        write_gpr(rd(insn), gpr_[rt(insn)] >> (gpr_[rs(insn)] & 31u));
        break;
    case FN_SRAV:
        write_gpr(rd(insn),
                  static_cast<u32>(static_cast<s32>(gpr_[rt(insn)]) >> (gpr_[rs(insn)] & 31u)));
        break;
    case FN_JR:
        branch_abs(gpr_[rs(insn)]);
        break;
    case FN_JALR: {
        const u32 t = gpr_[rs(insn)];
        const u32 d = rd(insn) == 0 ? 31u : rd(insn);
        link(d);
        branch_abs(t);
        break;
    }
    case FN_BREAK:
        do_break(insn);
        break;
    case FN_ADD:
    case FN_ADDU:
        write_gpr(rd(insn), gpr_[rs(insn)] + gpr_[rt(insn)]);
        break;
    case FN_SUB:
    case FN_SUBU:
        write_gpr(rd(insn), gpr_[rs(insn)] - gpr_[rt(insn)]);
        break;
    case FN_AND:
        write_gpr(rd(insn), gpr_[rs(insn)] & gpr_[rt(insn)]);
        break;
    case FN_OR:
        write_gpr(rd(insn), gpr_[rs(insn)] | gpr_[rt(insn)]);
        break;
    case FN_XOR:
        write_gpr(rd(insn), gpr_[rs(insn)] ^ gpr_[rt(insn)]);
        break;
    case FN_NOR:
        write_gpr(rd(insn), ~(gpr_[rs(insn)] | gpr_[rt(insn)]));
        break;
    case FN_SLT:
        write_gpr(rd(insn),
                  static_cast<s32>(gpr_[rs(insn)]) < static_cast<s32>(gpr_[rt(insn)]) ? 1u : 0u);
        break;
    case FN_SLTU:
        write_gpr(rd(insn), gpr_[rs(insn)] < gpr_[rt(insn)] ? 1u : 0u);
        break;
    default:
        N64_WARN("RSP unknown SPECIAL fn={:02X} @ {:03X}", fn(insn), pc_);
        break;
    }
}

void Rsp::exec_regimm(u32 insn) {
    const bool neg = static_cast<s32>(gpr_[rs(insn)]) < 0;
    switch (rt(insn)) {
    case RT_BLTZ:
        if (neg) branch_rel(simm(insn));
        break;
    case RT_BGEZ:
        if (!neg) branch_rel(simm(insn));
        break;
    case RT_BLTZAL:
        link(31);
        if (neg) branch_rel(simm(insn));
        break;
    case RT_BGEZAL:
        link(31);
        if (!neg) branch_rel(simm(insn));
        break;
    default:
        N64_WARN("RSP unknown REGIMM rt={:02X}", rt(insn));
        break;
    }
}

void Rsp::exec_cop0(u32 insn) {
    // RSP COP0: MFC0/MTC0 to SP / DPC registers (subset).
    const u32 co_rs = rs(insn);
    const u32 rd_f = rd(insn);
    const u32 rt_f = rt(insn);

    if (co_rs == 0) { // MFC0
        u32 val = 0;
        if (sp_ && bus_) {
            // SP_MEM_ADDR=0 ... SP_SEMAPHORE=7, DPC_START=8 ... DPC_TMEM=15.
            if (rd_f <= 7) {
                val = sp_->read(rd_f * 4);
                if (rd_f == 7) {
                    // semaphore side-effect
                    val = sp_->read_semaphore();
                }
            } else if (rd_f <= 15) {
                val = bus_->dp_regs().read((rd_f - 8) * 4);
            }
        }
        write_gpr(rt_f, val);
        return;
    }
    if (co_rs == 4) { // MTC0
        const u32 val = gpr_[rt_f];
        if (sp_ && bus_) {
            if (rd_f <= 7) {
                sp_->write(rd_f * 4, val);
                // Keep local halt flag in sync if STATUS written
                if (rd_f == 4) {
                    // STATUS was written; re-read halt bit
                    halted_ = (sp_->status() & SpRegisters::StHalt) != 0;
                    intr_on_break_ = (sp_->status() & SpRegisters::StIntrOnBreak) != 0;
                }
            } else if (rd_f <= 15) {
                bus_->dp_regs().write((rd_f - 8) * 4, val);
            }
        }
        return;
    }
    N64_TRACE("RSP COP0 rs={:02X} stub", co_rs);
}

void Rsp::exec_lwl(u32 insn) {
    const u32 addr = gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn)));
    const u32 aligned = addr & ~0x3u;
    const u32 mem = load_word(aligned);
    const u32 n = addr & 3u;
    const u32 mask = 0xFFFF'FFFFu << ((3u - n) * 8u);
    const u32 merged = mem << ((3u - n) * 8u);
    write_gpr(rt(insn), (gpr_[rt(insn)] & ~mask) | (merged & mask));
}

void Rsp::exec_lwr(u32 insn) {
    const u32 addr = gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn)));
    const u32 aligned = addr & ~0x3u;
    const u32 mem = load_word(aligned);
    const u32 n = addr & 3u;
    const u32 mask = 0xFFFF'FFFFu >> (n * 8u);
    const u32 merged = mem >> (n * 8u);
    write_gpr(rt(insn), (gpr_[rt(insn)] & ~mask) | (merged & mask));
}

void Rsp::exec_swl(u32 insn) {
    const u32 addr = gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn)));
    const u32 aligned = addr & ~0x3u;
    const u32 n = addr & 3u;
    const u32 reg = gpr_[rt(insn)];
    u32 mem = load_word(aligned);
    const u32 mask = 0xFFFF'FFFFu << ((3u - n) * 8u);
    mem = (mem & ~mask) | ((reg >> ((3u - n) * 8u)) & mask);
    store_word(aligned, mem);
}

void Rsp::exec_swr(u32 insn) {
    const u32 addr = gpr_[rs(insn)] + static_cast<u32>(static_cast<s32>(simm(insn)));
    const u32 aligned = addr & ~0x3u;
    const u32 n = addr & 3u;
    const u32 reg = gpr_[rt(insn)];
    u32 mem = load_word(aligned);
    const u32 mask = 0xFFFF'FFFFu >> (n * 8u);
    mem = (mem & ~mask) | ((reg << (n * 8u)) & mask);
    store_word(aligned, mem);
}

} // namespace n64
