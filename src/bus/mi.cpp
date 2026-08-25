#include "n64/bus/mi.hpp"

#include "n64/common/log.hpp"

namespace n64 {

void MipsInterface::reset() {
    mode_ = 0;
    // MI_VERSION: typical RCP version value used by homebrew/libultra checks.
    intr_ = 0;
    intr_mask_ = 0;
}

u32 MipsInterface::read(u32 offset) const {
    switch (offset & 0xFCu) {
    case Mode:     return mode_;
    case Version:  return 0x0202'0102u; // common MI_VERSION
    case Intr:     return intr_;
    case IntrMask: return intr_mask_;
    default:
        N64_TRACE("MI read unknown offset {:02X}", offset);
        return 0;
    }
}

void MipsInterface::write(u32 offset, u32 value) {
    switch (offset & 0xFCu) {
    case Mode:
        // Bits clear/set init length + various mode flags. Store raw for now.
        mode_ = value;
        break;
    case Version:
        // Read-only
        break;
    case Intr:
        // Read-only; devices clear via their own status registers.
        break;
    case IntrMask: {
        // Odd bits clear, even bits set (n64brew MI_INTR_MASK write format).
        // bit0 CLR_SP, bit1 SET_SP, bit2 CLR_SI, bit3 SET_SI, ...
        u32 mask = intr_mask_;
        for (u32 i = 0; i < 6; ++i) {
            const u32 clr = 1u << (i * 2);
            const u32 set = 1u << (i * 2 + 1);
            if (value & clr) {
                mask &= ~(1u << i);
            }
            if (value & set) {
                mask |= (1u << i);
            }
        }
        intr_mask_ = mask & 0x3Fu;
        notify();
        break;
    }
    default:
        N64_TRACE("MI write unknown offset {:02X} = {:08X}", offset, value);
        break;
    }
}

void MipsInterface::raise(u32 mask_bit) {
    intr_ |= mask_bit;
    notify();
}

void MipsInterface::clear(u32 mask_bit) {
    intr_ &= ~mask_bit;
    notify();
}

void MipsInterface::notify() {
    if (irq_cb_) {
        irq_cb_(cpu_irq_level());
    }
}

} // namespace n64
