#pragma once

#include "n64/bus/dp_regs.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/pi.hpp"
#include "n64/bus/ri.hpp"
#include "n64/bus/si.hpp"
#include "n64/bus/sp_regs.hpp"
#include "n64/common/types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace n64 {

class VideoInterface;
class AudioInterface;
class Pif;
class Rsp;
class Rdp;
class Cpu;
class Debugger;

/// Physical memory map + MMIO dispatch + DMA engines.
class Bus {
public:
    explicit Bus(std::size_t rdram_size = kRdramSize);
    ~Bus() = default;

    Bus(const Bus&) = delete;
    Bus& operator=(const Bus&) = delete;

    void reset();

    /// Wire peripheral objects owned by Emulator. Safe to call multiple times.
    void connect(Cpu* cpu,
                 VideoInterface* vi,
                 AudioInterface* ai,
                 Pif* pif,
                 Rsp* rsp,
                 Rdp* rdp);

    void set_debugger(Debugger* dbg) noexcept { debugger_ = dbg; }
    [[nodiscard]] Debugger* debugger() const noexcept { return debugger_; }

    /// Load raw cartridge bytes (big-endian .z64 preferred).
    [[nodiscard]] bool load_cartridge(std::span<const u8> data);
    [[nodiscard]] bool load_cartridge_file(std::string_view path);

    [[nodiscard]] u8  read8 (PhysicalAddress addr);
    [[nodiscard]] u16 read16(PhysicalAddress addr);
    [[nodiscard]] u32 read32(PhysicalAddress addr);
    [[nodiscard]] u64 read64(PhysicalAddress addr);

    void write8 (PhysicalAddress addr, u8  value);
    void write16(PhysicalAddress addr, u16 value);
    void write32(PhysicalAddress addr, u32 value);
    void write64(PhysicalAddress addr, u64 value);

    /// Notify the CPU block cache after a direct memory/DMA write that bypasses
    /// the typed write helpers.
    void notify_memory_write(PhysicalAddress paddr, u32 size);

    // Const overloads for tests that only touch RDRAM/cart/SP mem (no MMIO side effects).
    [[nodiscard]] u8  read8_const (PhysicalAddress addr) const;
    [[nodiscard]] u16 read16_const(PhysicalAddress addr) const;
    [[nodiscard]] u32 read32_const(PhysicalAddress addr) const;

    [[nodiscard]] std::size_t rdram_size() const noexcept { return rdram_.size(); }
    [[nodiscard]] std::span<u8> rdram() noexcept { return rdram_; }
    [[nodiscard]] std::span<const u8> rdram() const noexcept { return rdram_; }

    [[nodiscard]] std::span<u8> sp_dmem() noexcept { return sp_dmem_; }
    [[nodiscard]] std::span<const u8> sp_dmem() const noexcept { return sp_dmem_; }
    [[nodiscard]] std::span<u8> sp_imem() noexcept { return sp_imem_; }
    [[nodiscard]] std::span<const u8> sp_imem() const noexcept { return sp_imem_; }
    [[nodiscard]] std::span<u8> pif_ram() noexcept { return pif_ram_; }
    [[nodiscard]] std::span<const u8> pif_ram() const noexcept { return pif_ram_; }

    [[nodiscard]] std::span<const u8> cartridge() const noexcept { return cart_; }
    [[nodiscard]] bool has_cartridge() const noexcept { return !cart_.empty(); }

    /// Load a user-owned PIF boot ROM (typically 2 KiB) at physical 0x1FC00000.
    /// Never bundle proprietary dumps with this project.
    [[nodiscard]] bool load_pif_rom(std::span<const u8> data);
    [[nodiscard]] bool has_pif_rom() const noexcept { return !pif_rom_.empty(); }
    [[nodiscard]] std::span<const u8> pif_rom() const noexcept { return pif_rom_; }

    /// Read a byte from cartridge image by cart-bus address (0x10000000-relative
    /// or raw cart_addr with domain bits). Used by PI DMA.
    [[nodiscard]] u8 cart_read_byte(u32 cart_addr) const;
    void cart_write_byte(u32 cart_addr, u8 value);

    [[nodiscard]] MipsInterface& mi() noexcept { return mi_; }
    [[nodiscard]] const MipsInterface& mi() const noexcept { return mi_; }
    [[nodiscard]] PeripheralInterface& pi() noexcept { return pi_; }
    [[nodiscard]] const PeripheralInterface& pi() const noexcept { return pi_; }
    [[nodiscard]] SerialInterface& si() noexcept { return si_; }
    [[nodiscard]] const SerialInterface& si() const noexcept { return si_; }
    [[nodiscard]] RdramInterface& ri() noexcept { return ri_; }
    [[nodiscard]] SpRegisters& sp_regs() noexcept { return sp_regs_; }
    [[nodiscard]] const SpRegisters& sp_regs() const noexcept { return sp_regs_; }
    [[nodiscard]] DpRegisters& dp_regs() noexcept { return dp_regs_; }

    /// Mask physical address to 29-bit physical bus (N64 convention).
    static constexpr PhysicalAddress physical(Address a) noexcept {
        return a & 0x1FFF'FFFFu;
    }

private:
    [[nodiscard]] u8 read_open_bus(PhysicalAddress addr) const;
    [[nodiscard]] u32 read_mmio32(PhysicalAddress paddr);
    void write_mmio32(PhysicalAddress paddr, u32 value);

    /// Direct RDRAM byte access without MMIO (for DMA engines).
    friend class PeripheralInterface;
    friend class SerialInterface;
    friend class SpRegisters;

    std::vector<u8> rdram_;
    std::vector<u8> cart_;
    std::vector<u8> pif_rom_; // user-provided, optional (≤ 8 KiB)
    std::array<u8, kSpDmemSize> sp_dmem_{};
    std::array<u8, kSpImemSize> sp_imem_{};
    std::array<u8, kPifRamSize> pif_ram_{};

    MipsInterface mi_;
    PeripheralInterface pi_;
    SerialInterface si_;
    RdramInterface ri_;
    SpRegisters sp_regs_;
    DpRegisters dp_regs_;

    Cpu* cpu_ = nullptr;
    VideoInterface* vi_ = nullptr;
    AudioInterface* ai_ = nullptr;
    Pif* pif_ = nullptr;
    Rsp* rsp_ = nullptr;
    Rdp* rdp_ = nullptr;
    Debugger* debugger_ = nullptr;
};

} // namespace n64
