#include "n64/bus/pi.hpp"

#include "n64/bus/bus.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/common/log.hpp"

#include <algorithm>

namespace n64 {

void PeripheralInterface::reset() {
    dram_addr_ = 0;
    cart_addr_ = 0;
    rd_len_ = 0;
    wr_len_ = 0;
    status_ = 0;
    dom1_lat_ = 0x40;
    dom1_pwd_ = 0x12;
    dom1_pgs_ = 0x07;
    dom1_rls_ = 0x03;
    dom2_lat_ = 0x40;
    dom2_pwd_ = 0x12;
    dom2_pgs_ = 0x07;
    dom2_rls_ = 0x03;
    last_dma_bytes_ = 0;
}

u32 PeripheralInterface::read(u32 offset) const {
    switch (offset & 0x3Cu) {
    case DramAddr:   return dram_addr_ & 0x00FF'FFFEu;
    case CartAddr:   return cart_addr_ & 0xFFFF'FFFEu;
    case RdLen:      return rd_len_;
    case WrLen:      return wr_len_;
    case Status:     return status_;
    case BsdDom1Lat: return dom1_lat_;
    case BsdDom1Pwd: return dom1_pwd_;
    case BsdDom1Pgs: return dom1_pgs_;
    case BsdDom1Rls: return dom1_rls_;
    case BsdDom2Lat: return dom2_lat_;
    case BsdDom2Pwd: return dom2_pwd_;
    case BsdDom2Pgs: return dom2_pgs_;
    case BsdDom2Rls: return dom2_rls_;
    default:
        return 0;
    }
}

void PeripheralInterface::write(u32 offset, u32 value) {
    switch (offset & 0x3Cu) {
    case DramAddr:
        dram_addr_ = value & 0x00FF'FFFFu;
        break;
    case CartAddr:
        cart_addr_ = value;
        break;
    case RdLen:
        rd_len_ = value & 0x00FF'FFFFu;
        start_dma_to_rdram();
        break;
    case WrLen:
        wr_len_ = value & 0x00FF'FFFFu;
        start_dma_to_cart();
        break;
    case Status:
        if (value & WrReset) {
            status_ &= ~(StatusDmaBusy | StatusIoBusy | StatusError);
        }
        if (value & WrClrIntr) {
            status_ &= ~StatusIntr;
            if (mi_) {
                mi_->clear(mmio::MiIntr::PI);
            }
        }
        break;
    case BsdDom1Lat: dom1_lat_ = value & 0xFFu; break;
    case BsdDom1Pwd: dom1_pwd_ = value & 0xFFu; break;
    case BsdDom1Pgs: dom1_pgs_ = value & 0xFFu; break;
    case BsdDom1Rls: dom1_rls_ = value & 0xFFu; break;
    case BsdDom2Lat: dom2_lat_ = value & 0xFFu; break;
    case BsdDom2Pwd: dom2_pwd_ = value & 0xFFu; break;
    case BsdDom2Pgs: dom2_pgs_ = value & 0xFFu; break;
    case BsdDom2Rls: dom2_rls_ = value & 0xFFu; break;
    default:
        N64_TRACE("PI write unknown {:02X} = {:08X}", offset, value);
        break;
    }
}

void PeripheralInterface::start_dma_to_rdram() {
    if (!bus_) {
        return;
    }
    // Length register encodes (bytes - 1).
    const u32 length = (rd_len_ & 0x00FF'FFFFu) + 1;
    u32 dram = dram_addr_ & ~0x1u;
    u32 cart = cart_addr_ & ~0x1u;

    status_ |= StatusDmaBusy;
    N64_DEBUG("PI DMA cart→RDRAM cart={:08X} dram={:08X} len={}", cart, dram, length);

    for (u32 i = 0; i < length; ++i) {
        const u8 b = bus_->cart_read_byte(cart + i);
        if (!bus_->rdram().empty()) {
            bus_->rdram()[(dram + i) % bus_->rdram_size()] = b;
        }
    }
    bus_->notify_memory_write(dram, length);

    // Hardware advances addresses; store end-ish values.
    dram_addr_ = dram + length;
    cart_addr_ = cart + length;
    last_dma_bytes_ = length;
    finish_dma();
}

void PeripheralInterface::start_dma_to_cart() {
    if (!bus_) {
        return;
    }
    const u32 length = (wr_len_ & 0x00FF'FFFFu) + 1;
    u32 dram = dram_addr_ & ~0x1u;
    u32 cart = cart_addr_ & ~0x1u;

    status_ |= StatusDmaBusy;
    N64_DEBUG("PI DMA RDRAM→cart dram={:08X} cart={:08X} len={}", dram, cart, length);

    for (u32 i = 0; i < length; ++i) {
        u8 b = 0;
        if (!bus_->rdram().empty()) {
            b = bus_->rdram()[(dram + i) % bus_->rdram_size()];
        }
        bus_->cart_write_byte(cart + i, b);
    }

    dram_addr_ = dram + length;
    cart_addr_ = cart + length;
    last_dma_bytes_ = length;
    finish_dma();
}

void PeripheralInterface::finish_dma() {
    status_ &= ~StatusDmaBusy;
    status_ |= StatusIntr;
    if (mi_) {
        mi_->raise(mmio::MiIntr::PI);
    }
}

} // namespace n64
