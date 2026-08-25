#pragma once

#include "n64/common/types.hpp"

#include <cstdint>

namespace n64 {

/// RDRAM Interface registers (mostly stubbed; homebrew rarely depends on them).
/// Physical base 0x0470'0000.
class RdramInterface {
public:
    enum Reg : u32 {
        Mode   = 0x00,
        Config = 0x04,
        CurrentLoad = 0x08,
        Select = 0x0C,
        Refresh = 0x10,
        Latency = 0x14,
        Rerror = 0x18,
        Werror = 0x1C,
    };

    void reset();

    [[nodiscard]] u32 read(u32 offset) const;
    void write(u32 offset, u32 value);

private:
    u32 mode_ = 0x0E;
    u32 config_ = 0x40;
    u32 current_load_ = 0;
    u32 select_ = 0x14;
    u32 refresh_ = 0x0006'3624;
    u32 latency_ = 0x15;
    u32 rerror_ = 0;
    u32 werror_ = 0;
};

} // namespace n64
