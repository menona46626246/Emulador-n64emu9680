#pragma once

#include "n64/common/types.hpp"

#include <cstdint>

namespace n64 {

class Bus;
class MipsInterface;
class Rdp;

/// DP (RDP) command registers.
/// Physical base 0x0410'0000.
class DpRegisters {
public:
    enum Reg : u32 {
        Start    = 0x00,
        End      = 0x04,
        Current  = 0x08,
        Status   = 0x0C,
        Clock    = 0x10,
        BufBusy  = 0x14,
        PipeBusy = 0x18,
        Tmem     = 0x1C,
    };

    static constexpr u32 StXbusDma    = 1u << 0;
    static constexpr u32 StFreeze     = 1u << 1;
    static constexpr u32 StFlush      = 1u << 2;
    static constexpr u32 StStartGclk  = 1u << 3;
    static constexpr u32 StTmemBusy   = 1u << 4;
    static constexpr u32 StPipeBusy   = 1u << 5;
    static constexpr u32 StCmdBusy    = 1u << 6;
    static constexpr u32 StCbufReady  = 1u << 7;
    static constexpr u32 StDmaBusy    = 1u << 8;
    static constexpr u32 StEndValid   = 1u << 9;
    static constexpr u32 StStartValid = 1u << 10;

    void reset();
    void connect(Bus* bus, MipsInterface* mi, Rdp* rdp) noexcept {
        bus_ = bus;
        mi_ = mi;
        rdp_ = rdp;
    }

    [[nodiscard]] u32 read(u32 offset) const;
    void write(u32 offset, u32 value);

    [[nodiscard]] u32 start() const noexcept { return start_; }
    [[nodiscard]] u32 end() const noexcept { return end_; }

private:
    void run_commands();

    Bus* bus_ = nullptr;
    MipsInterface* mi_ = nullptr;
    Rdp* rdp_ = nullptr;

    u32 start_ = 0;
    u32 end_ = 0;
    u32 current_ = 0;
    u32 status_ = StCbufReady;
};

} // namespace n64
