#include "n64/bus/bus.hpp"

#include "n64/ai/ai.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/common/log.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/cpu/block_cache.hpp"
#include "n64/debug/debugger.hpp"
#include "n64/pif/pif.hpp"
#include "n64/rcp/rdp/rdp.hpp"
#include "n64/rcp/rsp/rsp.hpp"
#include "n64/vi/vi.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace n64 {
namespace {

void normalize_cart_endian(std::vector<u8>& data) {
    if (data.size() < 4) {
        return;
    }
    const u32 magic = (static_cast<u32>(data[0]) << 24) |
                      (static_cast<u32>(data[1]) << 16) |
                      (static_cast<u32>(data[2]) << 8)  |
                      (static_cast<u32>(data[3]));
    if (magic == 0x80371240u) {
        return;
    }
    if (magic == 0x37804012u) {
        for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
            std::swap(data[i], data[i + 1]);
        }
        N64_INFO("Cartridge converted from .v64 (byte-swapped) to .z64");
        return;
    }
    if (magic == 0x40123780u) {
        for (std::size_t i = 0; i + 3 < data.size(); i += 4) {
            std::swap(data[i + 0], data[i + 3]);
            std::swap(data[i + 1], data[i + 2]);
        }
        N64_INFO("Cartridge converted from .n64 (little-endian) to .z64");
        return;
    }
    N64_WARN("Unknown cartridge magic {:08X}; leaving bytes as-is", magic);
}

[[nodiscard]] bool is_mmio_region(PhysicalAddress p) noexcept {
    using namespace mmio;
    if (p >= SP_REGS_BASE && p < SP_REGS_BASE + 0x20) return true;
    if (p >= SP_REGS2_BASE && p < SP_REGS2_BASE + 0x10) return true;
    if (p >= DP_CMD_BASE && p < DP_CMD_BASE + 0x20) return true;
    if (p >= MI_BASE && p < MI_BASE + 0x10) return true;
    if (p >= VI_BASE && p < VI_BASE + 0x40) return true;
    if (p >= AI_BASE && p < AI_BASE + 0x20) return true;
    if (p >= PI_BASE && p < PI_BASE + 0x40) return true;
    if (p >= RI_BASE && p < RI_BASE + 0x20) return true;
    if (p >= SI_BASE && p < SI_BASE + 0x20) return true;
    return false;
}

} // namespace

Bus::Bus(std::size_t rdram_size) : rdram_(rdram_size, 0) {}

void Bus::reset() {
    std::fill(rdram_.begin(), rdram_.end(), static_cast<u8>(0));
    sp_dmem_.fill(0);
    sp_imem_.fill(0);
    pif_ram_.fill(0);
    // cart_ and pif_rom_ are preserved across soft reset (user-provided images).
    mi_.reset();
    pi_.reset();
    si_.reset();
    ri_.reset();
    sp_regs_.reset();
    dp_regs_.reset();
}

void Bus::connect(Cpu* cpu,
                  VideoInterface* vi,
                  AudioInterface* ai,
                  Pif* pif,
                  Rsp* rsp,
                  Rdp* rdp) {
    cpu_ = cpu;
    vi_ = vi;
    ai_ = ai;
    pif_ = pif;
    rsp_ = rsp;
    rdp_ = rdp;

    pi_.connect(this, &mi_);
    si_.connect(this, &mi_, pif_);
    sp_regs_.connect(this, &mi_, rsp_);
    dp_regs_.connect(this, &mi_, rdp_);
    if (rdp_) {
        rdp_->connect(this, &mi_);
    }
    if (rsp_) {
        rsp_->connect(this, &sp_regs_, &mi_);
    }

    if (vi_) {
        vi_->connect_mi(&mi_);
    }
    if (ai_) {
        ai_->connect(this, &mi_);
    }

    // MI → CPU IP2
    mi_.set_irq_callback([this](bool level) {
        if (cpu_) {
            cpu_->set_rcp_interrupt(level);
        }
    });
}

bool Bus::load_cartridge(std::span<const u8> data) {
    if (data.empty()) {
        N64_ERROR("Empty cartridge data");
        return false;
    }
    cart_.assign(data.begin(), data.end());
    normalize_cart_endian(cart_);
    return true;
}

bool Bus::load_pif_rom(std::span<const u8> data) {
    if (data.empty()) {
        N64_ERROR("Empty PIF ROM data");
        return false;
    }
    // Accept 2 KiB classic size and a bit of slack for padded dumps.
    if (data.size() > 8 * 1024) {
        N64_ERROR("PIF ROM too large ({} bytes)", data.size());
        return false;
    }
    pif_rom_.assign(data.begin(), data.end());
    return true;
}

bool Bus::load_cartridge_file(std::string_view path) {
    std::ifstream in(std::string(path), std::ios::binary | std::ios::ate);
    if (!in) {
        N64_ERROR("Cannot open cartridge file: {}", path);
        return false;
    }
    const auto size = in.tellg();
    if (size <= 0) {
        N64_ERROR("Cartridge file is empty: {}", path);
        return false;
    }
    in.seekg(0, std::ios::beg);
    std::vector<u8> data(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(data.data()), size)) {
        N64_ERROR("Failed reading cartridge file: {}", path);
        return false;
    }
    return load_cartridge(data);
}

u8 Bus::cart_read_byte(u32 cart_addr) const {
    // Cart addresses on the PI bus: typically 0x10000000+ for DOM1.
    u32 offset = cart_addr;
    if (offset >= mmio::CART_DOM1_BASE) {
        offset -= mmio::CART_DOM1_BASE;
    }
    // Also accept raw 0x00000000-based offsets used by some DMA setups.
    if (offset < cart_.size()) {
        return cart_[offset];
    }
    return 0;
}

void Bus::cart_write_byte(u32 cart_addr, u8 value) {
    u32 offset = cart_addr;
    if (offset >= mmio::CART_DOM1_BASE) {
        offset -= mmio::CART_DOM1_BASE;
    }
    // Flash/SRAM writes later; for now allow writing into cart image buffer
    // if present (useful for tests). Extend vector only within reasonable size.
    if (offset < cart_.size()) {
        cart_[offset] = value;
    }
}

u8 Bus::read_open_bus(PhysicalAddress addr) const {
    return static_cast<u8>((addr >> 8) & 0xFF);
}

u8 Bus::read8_const(PhysicalAddress addr) const {
    const PhysicalAddress paddr = physical(addr);

    if (paddr < 0x03F0'0000u) {
        if (rdram_.empty()) return read_open_bus(paddr);
        return rdram_[paddr % rdram_.size()];
    }
    if (paddr >= 0x0400'0000u && paddr < 0x0400'1000u) {
        return sp_dmem_[paddr - 0x0400'0000u];
    }
    if (paddr >= 0x0400'1000u && paddr < 0x0400'2000u) {
        return sp_imem_[paddr - 0x0400'1000u];
    }
    if (paddr >= mmio::CART_DOM1_BASE && paddr < mmio::CART_DOM1_END + 1) {
        const std::size_t off = static_cast<std::size_t>(paddr - mmio::CART_DOM1_BASE);
        if (off < cart_.size()) return cart_[off];
        return 0;
    }
    // PIF ROM 0x1FC00000 – 0x1FC007BF (before PIF RAM)
    if (paddr >= mmio::PIF_ROM_BASE && paddr < mmio::PIF_RAM_BASE) {
        const std::size_t off = static_cast<std::size_t>(paddr - mmio::PIF_ROM_BASE);
        if (off < pif_rom_.size()) {
            return pif_rom_[off];
        }
        // Open bus / zero if no user PIF ROM loaded (HLE boot never executes here).
        return 0;
    }
    if (paddr >= mmio::PIF_RAM_BASE && paddr <= mmio::PIF_RAM_END) {
        const auto off = paddr - mmio::PIF_RAM_BASE;
        if (pif_) {
            return pif_->ram()[off];
        }
        return pif_ram_[off];
    }
    return read_open_bus(paddr);
}

u16 Bus::read16_const(PhysicalAddress addr) const {
    const PhysicalAddress paddr = physical(addr);
    if (paddr < 0x03F0'0000u && !rdram_.empty()) {
        const std::size_t off = paddr % rdram_.size();
        if (off + 1 < rdram_.size()) {
            u16 raw;
            std::memcpy(&raw, &rdram_[off], sizeof(raw));
            return bswap16(raw);
        }
    }
    const u8 b0 = read8_const(addr);
    const u8 b1 = read8_const(addr + 1);
    return static_cast<u16>((static_cast<u16>(b0) << 8) | b1);
}

u32 Bus::read32_const(PhysicalAddress addr) const {
    const PhysicalAddress paddr = physical(addr);
    if (paddr < 0x03F0'0000u && !rdram_.empty()) {
        const std::size_t off = paddr % rdram_.size();
        if (off + 3 < rdram_.size()) {
            u32 raw;
            std::memcpy(&raw, &rdram_[off], sizeof(raw));
            return bswap32(raw);
        }
    }
    if (paddr >= mmio::CART_DOM1_BASE && paddr <= mmio::CART_DOM1_END) {
        const std::size_t off = static_cast<std::size_t>(paddr - mmio::CART_DOM1_BASE);
        if (off + 3 < cart_.size()) {
            u32 raw;
            std::memcpy(&raw, &cart_[off], sizeof(raw));
            return bswap32(raw);
        }
    }
    if (paddr >= 0x0400'0000u && paddr < 0x0400'1000u) {
        const u32 off = paddr - 0x0400'0000u;
        if (off + 3 < sp_dmem_.size()) {
            u32 raw;
            std::memcpy(&raw, &sp_dmem_[off], sizeof(raw));
            return bswap32(raw);
        }
    }
    if (paddr >= 0x0400'1000u && paddr < 0x0400'2000u) {
        const u32 off = paddr - 0x0400'1000u;
        if (off + 3 < sp_imem_.size()) {
            u32 raw;
            std::memcpy(&raw, &sp_imem_[off], sizeof(raw));
            return bswap32(raw);
        }
    }
    return (static_cast<u32>(read8_const(addr)) << 24) |
           (static_cast<u32>(read8_const(addr + 1)) << 16) |
           (static_cast<u32>(read8_const(addr + 2)) << 8) |
           (static_cast<u32>(read8_const(addr + 3)));
}

u32 Bus::read_mmio32(PhysicalAddress paddr) {
    using namespace mmio;
    if (paddr >= SP_REGS_BASE && paddr < SP_REGS_BASE + 0x20) {
        const u32 off = paddr - SP_REGS_BASE;
        if ((off & 0x1Cu) == SpRegisters::Semaphore) {
            return sp_regs_.read_semaphore();
        }
        return sp_regs_.read(off);
    }
    if (paddr >= SP_REGS2_BASE && paddr < SP_REGS2_BASE + 0x10) {
        return sp_regs_.read_pc(paddr - SP_REGS2_BASE);
    }
    if (paddr >= DP_CMD_BASE && paddr < DP_CMD_BASE + 0x20) {
        return dp_regs_.read(paddr - DP_CMD_BASE);
    }
    if (paddr >= MI_BASE && paddr < MI_BASE + 0x10) {
        return mi_.read(paddr - MI_BASE);
    }
    if (paddr >= VI_BASE && paddr < VI_BASE + 0x40) {
        return vi_ ? vi_->read(paddr - VI_BASE) : 0;
    }
    if (paddr >= AI_BASE && paddr < AI_BASE + 0x20) {
        return ai_ ? ai_->read(paddr - AI_BASE) : 0;
    }
    if (paddr >= PI_BASE && paddr < PI_BASE + 0x40) {
        return pi_.read(paddr - PI_BASE);
    }
    if (paddr >= RI_BASE && paddr < RI_BASE + 0x20) {
        return ri_.read(paddr - RI_BASE);
    }
    if (paddr >= SI_BASE && paddr < SI_BASE + 0x20) {
        return si_.read(paddr - SI_BASE);
    }
    return 0;
}

void Bus::write_mmio32(PhysicalAddress paddr, u32 value) {
    using namespace mmio;
    if (paddr >= SP_REGS_BASE && paddr < SP_REGS_BASE + 0x20) {
        sp_regs_.write(paddr - SP_REGS_BASE, value);
        return;
    }
    if (paddr >= SP_REGS2_BASE && paddr < SP_REGS2_BASE + 0x10) {
        sp_regs_.write_pc(paddr - SP_REGS2_BASE, value);
        return;
    }
    if (paddr >= DP_CMD_BASE && paddr < DP_CMD_BASE + 0x20) {
        dp_regs_.write(paddr - DP_CMD_BASE, value);
        return;
    }
    if (paddr >= MI_BASE && paddr < MI_BASE + 0x10) {
        mi_.write(paddr - MI_BASE, value);
        return;
    }
    if (paddr >= VI_BASE && paddr < VI_BASE + 0x40) {
        if (vi_) vi_->write(paddr - VI_BASE, value);
        return;
    }
    if (paddr >= AI_BASE && paddr < AI_BASE + 0x20) {
        if (ai_) ai_->write(paddr - AI_BASE, value);
        return;
    }
    if (paddr >= PI_BASE && paddr < PI_BASE + 0x40) {
        pi_.write(paddr - PI_BASE, value);
        return;
    }
    if (paddr >= RI_BASE && paddr < RI_BASE + 0x20) {
        ri_.write(paddr - RI_BASE, value);
        return;
    }
    if (paddr >= SI_BASE && paddr < SI_BASE + 0x20) {
        si_.write(paddr - SI_BASE, value);
        return;
    }
    N64_TRACE("MMIO write unmapped {:08X} = {:08X}", paddr, value);
    if (debugger_) {
        debugger_->log_unknown_mmio(paddr, true, value);
    }
}

u8 Bus::read8(PhysicalAddress addr) {
    const PhysicalAddress paddr = physical(addr);
    if (debugger_) {
        debugger_->on_bus_read(paddr, 1);
    }
    if (is_mmio_region(paddr)) {
        // Byte reads of MMIO: extract from 32-bit register (big-endian lane).
        const u32 aligned = paddr & ~0x3u;
        const u32 word = read_mmio32(aligned);
        const u32 shift = (3u - (paddr & 3u)) * 8u;
        return static_cast<u8>((word >> shift) & 0xFF);
    }
    return read8_const(paddr);
}

u16 Bus::read16(PhysicalAddress addr) {
    const PhysicalAddress paddr = physical(addr);
    if (debugger_) {
        debugger_->on_bus_read(paddr, 2);
    }
    if (is_mmio_region(paddr & ~0x3u) || is_mmio_region(paddr)) {
        const u32 aligned = paddr & ~0x3u;
        const u32 word = read_mmio32(aligned);
        if ((paddr & 2u) == 0) {
            return static_cast<u16>((word >> 16) & 0xFFFF);
        }
        return static_cast<u16>(word & 0xFFFF);
    }
    return read16_const(paddr);
}

u32 Bus::read32(PhysicalAddress addr) {
    const PhysicalAddress paddr = physical(addr);
    if (debugger_) {
        debugger_->on_bus_read(paddr, 4);
    }
    if (is_mmio_region(paddr)) {
        return read_mmio32(paddr & ~0x3u);
    }
    return read32_const(paddr);
}

u64 Bus::read64(PhysicalAddress addr) {
    const PhysicalAddress paddr = physical(addr);
    if (debugger_) {
        debugger_->on_bus_read(paddr, 8);
    }
    if (paddr < 0x03F0'0000u && !rdram_.empty()) {
        const std::size_t off = paddr % rdram_.size();
        if (off + 7 < rdram_.size()) {
            u64 raw;
            std::memcpy(&raw, &rdram_[off], sizeof(raw));
            return bswap64(raw);
        }
    }
    const u64 hi = read32(addr);
    const u64 lo = read32(addr + 4);
    return (hi << 32) | lo;
}

void Bus::write8(PhysicalAddress addr, u8 value) {
    const PhysicalAddress paddr = physical(addr);
    notify_memory_write(paddr, 1);
    if (debugger_) {
        debugger_->on_bus_write(paddr, 1, value);
    }

    if (is_mmio_region(paddr)) {
        const u32 aligned = paddr & ~0x3u;
        u32 word = read_mmio32(aligned);
        const u32 shift = (3u - (paddr & 3u)) * 8u;
        word = (word & ~(0xFFu << shift)) | (static_cast<u32>(value) << shift);
        write_mmio32(aligned, word);
        return;
    }

    if (paddr < 0x03F0'0000u) {
        if (!rdram_.empty()) {
            rdram_[paddr % rdram_.size()] = value;
        }
        return;
    }
    if (paddr >= 0x0400'0000u && paddr < 0x0400'1000u) {
        sp_dmem_[paddr - 0x0400'0000u] = value;
        if (rsp_) rsp_->dmem()[paddr - 0x0400'0000u] = value;
        return;
    }
    if (paddr >= 0x0400'1000u && paddr < 0x0400'2000u) {
        sp_imem_[paddr - 0x0400'1000u] = value;
        if (rsp_) rsp_->imem()[paddr - 0x0400'1000u] = value;
        return;
    }
    if (paddr >= mmio::PIF_RAM_BASE && paddr <= mmio::PIF_RAM_END) {
        const auto off = paddr - mmio::PIF_RAM_BASE;
        pif_ram_[off] = value;
        if (pif_) {
            pif_->ram()[off] = value;
            // Writing the control byte with the process flag runs joybus HLE.
            if (off == 0x3F && (value & Pif::kCtrlProcess) != 0) {
                pif_->process_joybus();
                // Mirror responses back into the bus window.
                auto pr = pif_->ram();
                std::copy(pr.begin(), pr.end(), pif_ram_.begin());
            }
        }
        return;
    }
    N64_TRACE("write8 ignored @ {:08X} = {:02X}", paddr, value);
    if (debugger_) {
        debugger_->log_unknown_mmio(paddr, true, value);
    }
}

void Bus::write16(PhysicalAddress addr, u16 value) {
    const PhysicalAddress paddr = physical(addr);
    notify_memory_write(paddr, 2);
    if (debugger_) {
        debugger_->on_bus_write(paddr, 2, value);
    }
    if (is_mmio_region(paddr)) {
        const u32 aligned = paddr & ~0x3u;
        u32 word = read_mmio32(aligned);
        if ((paddr & 2u) == 0) {
            word = (word & 0x0000FFFFu) | (static_cast<u32>(value) << 16);
        } else {
            word = (word & 0xFFFF0000u) | static_cast<u32>(value);
        }
        write_mmio32(aligned, word);
        return;
    }
    if (paddr < 0x03F0'0000u && !rdram_.empty()) {
        const std::size_t off = paddr % rdram_.size();
        if (off + 1 < rdram_.size()) {
            const u16 swapped = bswap16(value);
            std::memcpy(&rdram_[off], &swapped, sizeof(swapped));
            return;
        }
    }
    if (paddr >= 0x0400'0000u && paddr < 0x0400'1000u) {
        const u32 off = paddr - 0x0400'0000u;
        if (off + 1 < sp_dmem_.size()) {
            const u16 swapped = bswap16(value);
            std::memcpy(&sp_dmem_[off], &swapped, sizeof(swapped));
            if (rsp_) std::memcpy(&rsp_->dmem()[off], &swapped, sizeof(swapped));
            return;
        }
    }
    write8(addr,     static_cast<u8>((value >> 8) & 0xFF));
    write8(addr + 1, static_cast<u8>(value & 0xFF));
}

void Bus::write32(PhysicalAddress addr, u32 value) {
    const PhysicalAddress paddr = physical(addr);
    notify_memory_write(paddr, 4);
    if (debugger_) {
        debugger_->on_bus_write(paddr, 4, value);
    }
    if (is_mmio_region(paddr)) {
        write_mmio32(paddr & ~0x3u, value);
        return;
    }
    if (paddr < 0x03F0'0000u && !rdram_.empty()) {
        const std::size_t base = paddr % rdram_.size();
        if (base + 3 < rdram_.size()) {
            const u32 swapped = bswap32(value);
            std::memcpy(&rdram_[base], &swapped, sizeof(swapped));
            return;
        }
    }
    if (paddr >= 0x0400'0000u && paddr < 0x0400'1000u) {
        const u32 off = paddr - 0x0400'0000u;
        if (off + 3 < sp_dmem_.size()) {
            const u32 swapped = bswap32(value);
            std::memcpy(&sp_dmem_[off], &swapped, sizeof(swapped));
            if (rsp_) std::memcpy(&rsp_->dmem()[off], &swapped, sizeof(swapped));
            return;
        }
    }
    if (paddr >= 0x0400'1000u && paddr < 0x0400'2000u) {
        const u32 off = paddr - 0x0400'1000u;
        if (off + 3 < sp_imem_.size()) {
            const u32 swapped = bswap32(value);
            std::memcpy(&sp_imem_[off], &swapped, sizeof(swapped));
            if (rsp_) std::memcpy(&rsp_->imem()[off], &swapped, sizeof(swapped));
            return;
        }
    }
    write8(addr,     static_cast<u8>((value >> 24) & 0xFF));
    write8(addr + 1, static_cast<u8>((value >> 16) & 0xFF));
    write8(addr + 2, static_cast<u8>((value >> 8) & 0xFF));
    write8(addr + 3, static_cast<u8>(value & 0xFF));
}

void Bus::write64(PhysicalAddress addr, u64 value) {
    const PhysicalAddress paddr = physical(addr);
    notify_memory_write(paddr, 8);
    if (debugger_) {
        debugger_->on_bus_write(paddr, 8, value);
    }
    if (paddr < 0x03F0'0000u && !rdram_.empty()) {
        const std::size_t base = paddr % rdram_.size();
        if (base + 7 < rdram_.size()) {
            const u64 swapped = bswap64(value);
            std::memcpy(&rdram_[base], &swapped, sizeof(swapped));
            return;
        }
    }
    write32(addr,     static_cast<u32>(value >> 32));
    write32(addr + 4, static_cast<u32>(value));
}

void Bus::notify_memory_write(PhysicalAddress paddr, u32 size) {
    if (!cpu_ || size == 0) {
        return;
    }
    BlockCache* cache = cpu_->block_cache();
    if (cache && cache->size() != 0) {
        cache->invalidate(physical(paddr), size);
    }
}

} // namespace n64
