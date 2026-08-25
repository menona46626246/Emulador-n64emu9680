#pragma once

#include "n64/bus/mmio.hpp"
#include "n64/common/types.hpp"

#include <array>
#include <cstdint>
#include <functional>

namespace n64 {

/// MIPS Interface — interrupt aggregation toward the VR4300.
/// Physical base 0x0430'0000.
class MipsInterface {
public:
    enum Reg : u32 {
        Mode     = 0x00,
        Version  = 0x04,
        Intr     = 0x08,
        IntrMask = 0x0C,
    };

    using IrqCallback = std::function<void(bool level)>;

    void reset();

    [[nodiscard]] u32 read(u32 offset) const;
    void write(u32 offset, u32 value);

    /// Raise / clear a device interrupt line (OR into MI_INTR).
    void raise(u32 mask_bit);
    void clear(u32 mask_bit);

    [[nodiscard]] u32 pending() const noexcept { return intr_; }
    [[nodiscard]] u32 mask() const noexcept { return intr_mask_; }
    [[nodiscard]] bool cpu_irq_level() const noexcept {
        return (intr_ & intr_mask_) != 0;
    }

    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }

private:
    void notify();

    u32 mode_ = 0;
    u32 intr_ = 0;
    u32 intr_mask_ = 0;
    IrqCallback irq_cb_;
};

} // namespace n64
