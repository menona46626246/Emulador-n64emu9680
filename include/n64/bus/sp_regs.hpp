#pragma once

#include "n64/common/types.hpp"

#include <cstdint>

namespace n64 {

class Bus;
class MipsInterface;
class Rsp;

/// SP (Signal Processor) register block + DMA engine.
/// Bases: 0x0404'0000 (regs), 0x0408'0000 (PC / IBIST).
class SpRegisters {
public:
    enum Reg : u32 {
        MemAddr   = 0x00,
        DramAddr  = 0x04,
        RdLen     = 0x08, // RDRAM → SP mem
        WrLen     = 0x0C, // SP mem → RDRAM
        Status    = 0x10,
        DmaFull   = 0x14,
        DmaBusy   = 0x18,
        Semaphore = 0x1C,
    };

    enum Reg2 : u32 {
        Pc    = 0x00,
        Ibist = 0x04,
    };

    // SP_STATUS read bits
    static constexpr u32 StHalt        = 1u << 0;
    static constexpr u32 StBroke       = 1u << 1;
    static constexpr u32 StDmaBusy     = 1u << 2;
    static constexpr u32 StDmaFull     = 1u << 3;
    static constexpr u32 StIoFull      = 1u << 4;
    static constexpr u32 StSingleStep  = 1u << 5;
    static constexpr u32 StIntrOnBreak = 1u << 6;
    static constexpr u32 StSignal0     = 1u << 7;
    static constexpr u32 StSignal1     = 1u << 8;
    static constexpr u32 StSignal2     = 1u << 9;
    static constexpr u32 StSignal3     = 1u << 10;
    static constexpr u32 StSignal4     = 1u << 11;
    static constexpr u32 StSignal5     = 1u << 12;
    static constexpr u32 StSignal6     = 1u << 13;
    static constexpr u32 StSignal7     = 1u << 14;

    // SP_STATUS write bits
    static constexpr u32 WrClearHalt      = 1u << 0;
    static constexpr u32 WrSetHalt        = 1u << 1;
    static constexpr u32 WrClearBroke     = 1u << 2;
    static constexpr u32 WrClearIntr      = 1u << 3;
    static constexpr u32 WrSetIntr        = 1u << 4;
    static constexpr u32 WrClearSStep     = 1u << 5;
    static constexpr u32 WrSetSStep       = 1u << 6;
    static constexpr u32 WrClearIntrBreak = 1u << 7;
    static constexpr u32 WrSetIntrBreak   = 1u << 8;

    void reset();
    void connect(Bus* bus, MipsInterface* mi, Rsp* rsp) noexcept {
        bus_ = bus;
        mi_ = mi;
        rsp_ = rsp;
    }

    [[nodiscard]] u32 read(u32 offset) const;
    void write(u32 offset, u32 value);

    /// Non-const read for SEMAPHORE side-effect (returns old, sets to 1).
    u32 read_semaphore();

    [[nodiscard]] u32 read_pc(u32 offset) const;
    void write_pc(u32 offset, u32 value);

    [[nodiscard]] u32 last_dma_bytes() const noexcept { return last_dma_bytes_; }
    [[nodiscard]] u32 status() const noexcept { return status_; }

    /// Called by RSP on BREAK to set broke/halt bits and optionally raise MI_SP.
    void notify_break(bool intr_on_break);

    /// Update halt/broke bits from RSP without full status write decode.
    void set_status_bits(u32 set_mask, u32 clear_mask);

private:
    void do_dma(bool to_sp, u32 len_reg);
    void apply_status_write(u32 value);

    Bus* bus_ = nullptr;
    MipsInterface* mi_ = nullptr;
    Rsp* rsp_ = nullptr;

    u32 mem_addr_ = 0;
    u32 dram_addr_ = 0;
    u32 status_ = StHalt;
    u32 semaphore_ = 0;
    u32 last_dma_bytes_ = 0;
};

} // namespace n64
