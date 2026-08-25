#include "n64/bus/ri.hpp"

#include "n64/common/log.hpp"

namespace n64 {

void RdramInterface::reset() {
    mode_ = 0x0E;
    config_ = 0x40;
    current_load_ = 0;
    select_ = 0x14;
    refresh_ = 0x0006'3624;
    latency_ = 0x15;
    rerror_ = 0;
    werror_ = 0;
}

u32 RdramInterface::read(u32 offset) const {
    switch (offset & 0x1Cu) {
    case Mode:        return mode_;
    case Config:      return config_;
    case CurrentLoad: return current_load_;
    case Select:      return select_;
    case Refresh:     return refresh_;
    case Latency:     return latency_;
    case Rerror:      return rerror_;
    case Werror:      return werror_;
    default:          return 0;
    }
}

void RdramInterface::write(u32 offset, u32 value) {
    switch (offset & 0x1Cu) {
    case Mode:        mode_ = value; break;
    case Config:      config_ = value; break;
    case CurrentLoad: current_load_ = value; break;
    case Select:      select_ = value; break;
    case Refresh:     refresh_ = value; break;
    case Latency:     latency_ = value; break;
    case Rerror:      rerror_ = 0; break; // write clears
    case Werror:      werror_ = 0; break;
    default:
        N64_TRACE("RI write unknown {:02X} = {:08X}", offset, value);
        break;
    }
}

} // namespace n64
