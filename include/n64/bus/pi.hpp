#pragma once

#include "n64/common/types.hpp"

#include <cstdint>
#include <functional>

namespace n64 {

class Bus;
class MipsInterface;

/// Peripheral Interface — cartridge DMA and domain configuration.
/// Physical base 0x0460'0000.
class PeripheralInterface {
public:
    enum Reg : u32 {
        DramAddr    = 0x00,
        CartAddr    = 0x04,
        RdLen       = 0x08, // cart → RDRAM
        WrLen       = 0x0C, // RDRAM → cart (EEPROM/SRAM later)
        Status      = 0x10,
        BsdDom1Lat  = 0x14,
        BsdDom1Pwd  = 0x18,
        BsdDom1Pgs  = 0x1C,
        BsdDom1Rls  = 0x20,
        BsdDom2Lat  = 0x24,
        BsdDom2Pwd  = 0x28,
        BsdDom2Pgs  = 0x2C,
        BsdDom2Rls  = 0x30,
    };

    // PI_STATUS bits
    static constexpr u32 StatusDmaBusy  = 1u << 0;
    static constexpr u32 StatusIoBusy   = 1u << 1;
    static constexpr u32 StatusError    = 1u << 2;
    static constexpr u32 StatusIntr     = 1u << 3; // interrupt pending (write-1-clear via wr)

    // PI_STATUS write bits
    static constexpr u32 WrClrIntr      = 1u << 1;
    static constexpr u32 WrReset        = 1u << 0;

    void reset();
    void connect(Bus* bus, MipsInterface* mi) noexcept {
        bus_ = bus;
        mi_ = mi;
    }

    [[nodiscard]] u32 read(u32 offset) const;
    void write(u32 offset, u32 value);

    /// Complete any pending DMA immediately (Phase 2: instantaneous DMA).
    /// Returns bytes transferred on the last DMA (0 if none).
    [[nodiscard]] u32 last_dma_bytes() const noexcept { return last_dma_bytes_; }

private:
    void start_dma_to_rdram();
    void start_dma_to_cart();
    void finish_dma();

    Bus* bus_ = nullptr;
    MipsInterface* mi_ = nullptr;

    u32 dram_addr_ = 0;
    u32 cart_addr_ = 0;
    u32 rd_len_ = 0;
    u32 wr_len_ = 0;
    u32 status_ = 0;

    u32 dom1_lat_ = 0x40;
    u32 dom1_pwd_ = 0x12;
    u32 dom1_pgs_ = 0x07;
    u32 dom1_rls_ = 0x03;
    u32 dom2_lat_ = 0x40;
    u32 dom2_pwd_ = 0x12;
    u32 dom2_pgs_ = 0x07;
    u32 dom2_rls_ = 0x03;

    u32 last_dma_bytes_ = 0;
};

} // namespace n64
