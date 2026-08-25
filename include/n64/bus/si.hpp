#pragma once

#include "n64/common/types.hpp"

#include <cstdint>

namespace n64 {

class Bus;
class MipsInterface;
class Pif;

/// Serial Interface — DMA between RDRAM and PIF RAM.
/// Physical base 0x0480'0000.
class SerialInterface {
public:
    enum Reg : u32 {
        DramAddr = 0x00,
        PifAddrRd64b = 0x04, // RDRAM ← PIF RAM
        // 0x08 unused
        // 0x0C unused
        PifAddrWr64b = 0x10, // RDRAM → PIF RAM
        // 0x14 unused
        Status = 0x18,
    };

    static constexpr u32 StatusDmaBusy  = 1u << 0;
    static constexpr u32 StatusIoBusy   = 1u << 1;
    static constexpr u32 StatusDmaError = 1u << 3;
    static constexpr u32 StatusIntr     = 1u << 12;

    void reset();
    void connect(Bus* bus, MipsInterface* mi, Pif* pif) noexcept {
        bus_ = bus;
        mi_ = mi;
        pif_ = pif;
    }

    [[nodiscard]] u32 read(u32 offset) const;
    void write(u32 offset, u32 value);

    [[nodiscard]] u32 last_dma_bytes() const noexcept { return last_dma_bytes_; }

private:
    void dma_from_pif();
    void dma_to_pif();
    void finish_dma();

    Bus* bus_ = nullptr;
    MipsInterface* mi_ = nullptr;
    Pif* pif_ = nullptr;

    u32 dram_addr_ = 0;
    u32 status_ = 0;
    u32 last_dma_bytes_ = 0;
};

} // namespace n64
