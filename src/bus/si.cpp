#include "n64/bus/si.hpp"

#include "n64/bus/bus.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/common/log.hpp"
#include "n64/pif/pif.hpp"

#include <algorithm>
#include <cstring>

namespace n64 {

void SerialInterface::reset() {
    dram_addr_ = 0;
    status_ = 0;
    last_dma_bytes_ = 0;
}

u32 SerialInterface::read(u32 offset) const {
    switch (offset & 0x1Cu) {
    case DramAddr:
        return dram_addr_ & 0x00FF'FFFFu;
    case PifAddrRd64b:
    case PifAddrWr64b:
        return 0;
    case Status:
        return status_;
    default:
        return 0;
    }
}

void SerialInterface::write(u32 offset, u32 value) {
    switch (offset & 0x1Cu) {
    case DramAddr:
        dram_addr_ = value & 0x00FF'FFFFu;
        break;
    case PifAddrRd64b:
        // Any write triggers 64-byte DMA PIF→RDRAM
        dma_from_pif();
        break;
    case PifAddrWr64b:
        dma_to_pif();
        break;
    case Status:
        // Write clears interrupt
        status_ &= ~StatusIntr;
        if (mi_) {
            mi_->clear(mmio::MiIntr::SI);
        }
        break;
    default:
        N64_TRACE("SI write unknown {:02X} = {:08X}", offset, value);
        break;
    }
}

void SerialInterface::dma_from_pif() {
    if (!bus_ || !pif_) {
        return;
    }
    status_ |= StatusDmaBusy;

    // If a joybus transaction was queued, ensure responses are filled before
    // the CPU reads them back. (Also covers software that only does PIF→RDRAM.)
    if (pif_->process_requested()) {
        pif_->process_joybus();
    }

    const u32 dram = dram_addr_ & ~0x7u;
    auto pif = pif_->ram();
    auto rdram = bus_->rdram();
    const u32 len = static_cast<u32>(kPifRamSize);
    for (u32 i = 0; i < len; ++i) {
        if (!rdram.empty()) {
            rdram[(dram + i) % rdram.size()] = pif[i];
        }
    }
    bus_->notify_memory_write(dram, len);
    // Mirror out
    auto bus_pif = bus_->pif_ram();
    std::copy(pif.begin(), pif.end(), bus_pif.begin());

    last_dma_bytes_ = len;
    N64_DEBUG("SI DMA PIF→RDRAM dram={:08X} len={}", dram, len);
    finish_dma();
}

void SerialInterface::dma_to_pif() {
    if (!bus_ || !pif_) {
        return;
    }
    status_ |= StatusDmaBusy;
    const u32 dram = dram_addr_ & ~0x7u;
    auto pif = pif_->ram();
    auto rdram = bus_->rdram();
    const u32 len = static_cast<u32>(kPifRamSize);
    for (u32 i = 0; i < len; ++i) {
        u8 b = 0;
        if (!rdram.empty()) {
            b = rdram[(dram + i) % rdram.size()];
        }
        pif[i] = b;
    }
    // Also mirror into bus pif_ram_ for direct MMIO view consistency.
    auto bus_pif = bus_->pif_ram();
    std::copy(pif.begin(), pif.end(), bus_pif.begin());

    // Run joybus HLE when the CPU set the process flag (PIF_RAM[0x3F] bit0).
    // Standard libultra flow: WR64 (commands + flag) → RD64 (responses).
    if (pif_->process_requested()) {
        pif_->process_joybus();
        std::copy(pif.begin(), pif.end(), bus_pif.begin());
    }

    last_dma_bytes_ = len;
    N64_DEBUG("SI DMA RDRAM→PIF dram={:08X} len={}", dram, len);
    finish_dma();
}

void SerialInterface::finish_dma() {
    status_ &= ~StatusDmaBusy;
    status_ |= StatusIntr;
    if (mi_) {
        mi_->raise(mmio::MiIntr::SI);
    }
}

} // namespace n64
