#include "n64/bus/dp_regs.hpp"

#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/common/log.hpp"
#include "n64/rcp/rdp/rdp.hpp"

namespace n64 {

void DpRegisters::reset() {
    start_ = 0;
    end_ = 0;
    current_ = 0;
    status_ = StCbufReady;
}

u32 DpRegisters::read(u32 offset) const {
    switch (offset & 0x1Cu) {
    case Start:    return start_;
    case End:      return end_;
    case Current:  return current_;
    case Status:   return status_;
    case Clock:    return 0;
    case BufBusy:  return 0;
    case PipeBusy: return 0;
    case Tmem:     return 0;
    default:       return 0;
    }
}

void DpRegisters::write(u32 offset, u32 value) {
    switch (offset & 0x1Cu) {
    case Start:
        start_ = value & 0x00FF'FFF8u;
        current_ = start_;
        status_ |= StStartValid;
        break;
    case End:
        end_ = value & 0x00FF'FFF8u;
        status_ |= StEndValid;
        // Writing END with a valid START runs the buffer (RDP pipeline kick).
        if ((status_ & StStartValid) && end_ > start_ && !(status_ & StFreeze)) {
            run_commands();
        }
        break;
    case Current:
        break;
    case Status: {
        // STATUS write: pairs of clear/set bits (simplified).
        if (value & (1u << 0)) status_ &= ~StXbusDma;
        if (value & (1u << 1)) status_ |= StXbusDma;
        if (value & (1u << 2)) status_ &= ~StFreeze;
        if (value & (1u << 3)) status_ |= StFreeze;
        if (value & (1u << 4)) status_ &= ~StFlush;
        if (value & (1u << 5)) {
            status_ |= StFlush;
            if (rdp_) {
                rdp_->flush();
            }
            if (mi_) {
                mi_->raise(mmio::MiIntr::DP);
            }
        }
        // bit 6/7 TMEM/pipe counter clear — ignore
        // bit 8/9 source (RDRAM vs DMEM) already via XBUS bit
        break;
    }
    default:
        N64_TRACE("DP write unknown {:02X} = {:08X}", offset, value);
        break;
    }
}

void DpRegisters::run_commands() {
    status_ |= StCmdBusy | StPipeBusy;
    status_ &= ~StCbufReady;

    const bool xbus = (status_ & StXbusDma) != 0;
    if (rdp_) {
        rdp_->run_command_list(start_, end_, xbus);
    }

    current_ = end_;
    // After a list runs, start tracks end (hardware advances).
    start_ = end_;
    status_ &= ~(StStartValid | StEndValid | StCmdBusy | StPipeBusy | StDmaBusy | StFlush);
    status_ |= StCbufReady;

    if (mi_) {
        mi_->raise(mmio::MiIntr::DP);
    }
    N64_DEBUG("DP run_commands done end={:08X} xbus={}", end_, xbus);
}

} // namespace n64
