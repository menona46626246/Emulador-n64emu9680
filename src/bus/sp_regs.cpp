#include "n64/bus/sp_regs.hpp"

#include "n64/bus/bus.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/common/log.hpp"
#include "n64/rcp/rsp/rsp.hpp"

#include <algorithm>

namespace n64 {
namespace {

struct SpDmaLen {
    u32 length;
    u32 count;
    u32 skip;
};

SpDmaLen decode_len(u32 value) noexcept {
    return SpDmaLen{
        (value & 0xFFFu) + 1,
        ((value >> 12) & 0xFFu) + 1,
        (value >> 20) & 0xFFFu,
    };
}

} // namespace

void SpRegisters::reset() {
    mem_addr_ = 0;
    dram_addr_ = 0;
    status_ = StHalt;
    semaphore_ = 0;
    last_dma_bytes_ = 0;
    if (rsp_) {
        rsp_->set_halted(true);
    }
}

u32 SpRegisters::read(u32 offset) const {
    switch (offset & 0x1Cu) {
    case MemAddr:  return mem_addr_ & 0x1FFFu;
    case DramAddr: return dram_addr_ & 0x00FF'FFFFu;
    case RdLen:
    case WrLen:    return 0;
    case Status:   return status_;
    case DmaFull:  return (status_ & StDmaFull) ? 1u : 0u;
    case DmaBusy:  return (status_ & StDmaBusy) ? 1u : 0u;
    case Semaphore: return semaphore_;
    default:       return 0;
    }
}

u32 SpRegisters::read_semaphore() {
    const u32 old = semaphore_;
    semaphore_ = 1;
    return old;
}

void SpRegisters::write(u32 offset, u32 value) {
    switch (offset & 0x1Cu) {
    case MemAddr:
        mem_addr_ = value & 0x1FFFu;
        break;
    case DramAddr:
        dram_addr_ = value & 0x00FF'FFFFu;
        break;
    case RdLen:
        do_dma(true, value);
        break;
    case WrLen:
        do_dma(false, value);
        break;
    case Status:
        apply_status_write(value);
        break;
    case Semaphore:
        semaphore_ = 0;
        break;
    default:
        N64_TRACE("SP write unknown {:02X} = {:08X}", offset, value);
        break;
    }
}

void SpRegisters::do_dma(bool to_sp, u32 len_reg) {
    if (!bus_) {
        return;
    }
    const SpDmaLen d = decode_len(len_reg);
    status_ |= StDmaBusy;

    u32 mem = mem_addr_ & 0x1FFFu;
    u32 dram = dram_addr_ & 0x00FF'FFFFu;
    const bool imem = (mem & 0x1000u) != 0;
    mem &= 0xFFFu;

    auto rdram = bus_->rdram();
    auto spmem = imem ? bus_->sp_imem() : bus_->sp_dmem();

    u32 total = 0;
    for (u32 line = 0; line < d.count; ++line) {
        const u32 line_mem = mem;
        const u32 line_dram = dram;
        for (u32 i = 0; i < d.length; ++i) {
            const u32 sp_off = (mem + i) & 0xFFFu;
            const u32 dr_off = dram + i;
            if (to_sp) {
                u8 b = 0;
                if (!rdram.empty()) {
                    b = rdram[dr_off % rdram.size()];
                }
                spmem[sp_off] = b;
            } else {
                const u8 b = spmem[sp_off];
                if (!rdram.empty()) {
                    rdram[dr_off % rdram.size()] = b;
                }
            }
            ++total;
        }
        if (to_sp) {
            bus_->notify_memory_write((imem ? 0x0400'1000u : 0x0400'0000u) + line_mem,
                                      d.length);
        } else {
            bus_->notify_memory_write(line_dram, d.length);
        }
        mem = (mem + d.length) & 0xFFFu;
        dram = dram + d.length + d.skip;
    }

    if (rsp_) {
        if (imem) {
            std::copy(spmem.begin(), spmem.end(), rsp_->imem().begin());
        } else {
            std::copy(spmem.begin(), spmem.end(), rsp_->dmem().begin());
        }
    }

    mem_addr_ = (imem ? 0x1000u : 0u) | mem;
    dram_addr_ = dram & 0x00FF'FFFFu;
    last_dma_bytes_ = total;
    status_ &= ~StDmaBusy;

    N64_DEBUG("SP DMA {} len={} count={} total={} imem={}",
              to_sp ? "RDRAM→SP" : "SP→RDRAM", d.length, d.count, total, imem);

    if (mi_) {
        mi_->raise(mmio::MiIntr::SP);
    }
}

u32 SpRegisters::read_pc(u32 offset) const {
    switch (offset & 0x0Cu) {
    case Pc:    return rsp_ ? (rsp_->pc() & 0xFFCu) : 0;
    case Ibist: return 0;
    default:    return 0;
    }
}

void SpRegisters::write_pc(u32 offset, u32 value) {
    if ((offset & 0x0Cu) == Pc && rsp_) {
        rsp_->set_pc(value & 0xFFCu);
    }
}

void SpRegisters::notify_break(bool intr_on_break) {
    status_ |= StBroke | StHalt;
    if (intr_on_break && mi_) {
        mi_->raise(mmio::MiIntr::SP);
    }
}

void SpRegisters::set_status_bits(u32 set_mask, u32 clear_mask) {
    status_ = (status_ & ~clear_mask) | set_mask;
}

void SpRegisters::apply_status_write(u32 value) {
    if (value & WrClearHalt) {
        status_ &= ~StHalt;
        status_ &= ~StBroke; // often cleared together when starting a task
        if (rsp_) {
            rsp_->set_halted(false);
            rsp_->set_broke(false);
            // Ensure RSP sees latest IMEM/DMEM from bus after CPU DMA.
            rsp_->pull_mem_from_bus();
        }
    }
    if (value & WrSetHalt) {
        status_ |= StHalt;
        if (rsp_) rsp_->set_halted(true);
    }
    if (value & WrClearBroke) {
        status_ &= ~StBroke;
        if (rsp_) rsp_->set_broke(false);
    }
    if (value & WrClearIntr) {
        if (mi_) mi_->clear(mmio::MiIntr::SP);
    }
    if (value & WrSetIntr) {
        if (mi_) mi_->raise(mmio::MiIntr::SP);
    }
    if (value & WrClearSStep) status_ &= ~StSingleStep;
    if (value & WrSetSStep)   status_ |= StSingleStep;
    if (value & WrClearIntrBreak) {
        status_ &= ~StIntrOnBreak;
        if (rsp_) rsp_->set_intr_on_break(false);
    }
    if (value & WrSetIntrBreak) {
        status_ |= StIntrOnBreak;
        if (rsp_) rsp_->set_intr_on_break(true);
    }

    for (u32 i = 0; i < 8; ++i) {
        const u32 clr = 1u << (9 + i * 2);
        const u32 set = 1u << (10 + i * 2);
        const u32 bit = StSignal0 << i;
        if (value & clr) status_ &= ~bit;
        if (value & set) status_ |= bit;
    }
}

} // namespace n64
