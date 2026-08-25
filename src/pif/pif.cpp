#include "n64/pif/pif.hpp"

#include "n64/common/log.hpp"

#include <algorithm>
#include <cstring>

namespace n64 {

void Pif::reset() {
    pif_ram_.fill(0);
    for (auto& c : controllers_) {
        c = ControllerState{};
    }
    controllers_[0].present = true;
    for (std::size_t i = 1; i < kChannels; ++i) {
        controllers_[i].present = false;
    }
    joybus_count_ = 0;
}

void Pif::set_controller(std::size_t channel, const ControllerState& state) {
    if (channel < kChannels) {
        controllers_[channel] = state;
    }
}

ControllerState Pif::controller(std::size_t channel) const {
    if (channel < kChannels) {
        return controllers_[channel];
    }
    return ControllerState{0, 0, 0, false};
}

bool Pif::process_channel(std::size_t channel, std::size_t& cursor) {
    auto& ram = pif_ram_;

    // Skip 0xFF padding between channels.
    while (cursor < 0x3F && ram[cursor] == 0xFF) {
        ++cursor;
    }

    if (cursor >= 0x3F) {
        return false;
    }

    // Terminator 0xFE ends the channel list.
    if (ram[cursor] == 0xFE) {
        return false;
    }

    // Channel skip 0x00 0x00 (no device address) — or tlen=0.
    const u8 tlen = ram[cursor];
    if (tlen == 0x00) {
        // Format: 00 00 skips this channel (no tx/rx).
        if (cursor + 1 < 0x3F && ram[cursor + 1] == 0x00) {
            cursor += 2;
            return true; // continue to next channel
        }
        // Lone 0x00: skip one byte.
        ++cursor;
        return true;
    }

    // Special 0xFD = reset channel / nop used by some libs — skip.
    if (tlen == 0xFD) {
        ++cursor;
        return true;
    }

    if (cursor + 1 >= 0x3F) {
        return false;
    }
    const u8 rlen = ram[cursor + 1];

    // rlen bit7 = device not present error flag (filled by PIF on response).
    // rlen bit6 = cannot receive error.
    // Mask off error bits for expected payload length.
    const u8 rlen_bytes = rlen & 0x3Fu;
    const u8 tlen_bytes = tlen & 0x3Fu;

    const std::size_t tx_off = cursor + 2;
    const std::size_t rx_off = tx_off + tlen_bytes;

    if (rx_off + rlen_bytes > 0x3F) {
        N64_WARN("Joybus channel {} command overruns PIF RAM (t={} r={} @{})",
                 channel, tlen_bytes, rlen_bytes, cursor);
        return false;
    }

    const u8 cmd = (tlen_bytes > 0) ? ram[tx_off] : 0x00;
    auto& pad = controllers_[channel];

    // Default: mark no error in rlen high bits.
    ram[cursor + 1] = rlen_bytes;

    if (!pad.present) {
        // Device absent: set error bit 7 on rlen, leave rx zeroed.
        ram[cursor + 1] = static_cast<u8>(rlen_bytes | 0x80u);
        std::fill(ram.begin() + static_cast<std::ptrdiff_t>(rx_off),
                  ram.begin() + static_cast<std::ptrdiff_t>(rx_off + rlen_bytes),
                  static_cast<u8>(0));
        cursor = rx_off + rlen_bytes;
        return true;
    }

    switch (cmd) {
    case JoybusCmd::Info:
    case JoybusCmd::Reset:
        // Response: type_lo, type_hi, status  (3 bytes)
        if (rlen_bytes >= 3) {
            ram[rx_off + 0] = static_cast<u8>(kControllerType & 0xFF);
            ram[rx_off + 1] = static_cast<u8>((kControllerType >> 8) & 0xFF);
            ram[rx_off + 2] = kControllerStatus;
            for (u8 i = 3; i < rlen_bytes; ++i) {
                ram[rx_off + i] = 0;
            }
        }
        break;

    case JoybusCmd::Controller:
        // Response: buttons_hi, buttons_lo, stick_x, stick_y  (4 bytes)
        // buttons are big-endian u16 in the two bytes.
        if (rlen_bytes >= 4) {
            ram[rx_off + 0] = static_cast<u8>((pad.buttons >> 8) & 0xFF);
            ram[rx_off + 1] = static_cast<u8>(pad.buttons & 0xFF);
            ram[rx_off + 2] = static_cast<u8>(pad.stick_x);
            ram[rx_off + 3] = static_cast<u8>(pad.stick_y);
            for (u8 i = 4; i < rlen_bytes; ++i) {
                ram[rx_off + i] = 0;
            }
        }
        break;

    case JoybusCmd::ReadMemPak:
    case JoybusCmd::WriteMemPak:
        // No pak: return zeroes / ack. Status already says no pak.
        std::fill(ram.begin() + static_cast<std::ptrdiff_t>(rx_off),
                  ram.begin() + static_cast<std::ptrdiff_t>(rx_off + rlen_bytes),
                  static_cast<u8>(0));
        break;

    case JoybusCmd::ReadEeprom:
    case JoybusCmd::WriteEeprom:
        // EEPROM is on channel 4 area of PIF protocol; for standard slots
        // return empty. Games that need EEPROM will be Phase 10+.
        std::fill(ram.begin() + static_cast<std::ptrdiff_t>(rx_off),
                  ram.begin() + static_cast<std::ptrdiff_t>(rx_off + rlen_bytes),
                  static_cast<u8>(0));
        break;

    default:
        N64_DEBUG("Joybus ch{} unknown cmd {:02X}", channel, cmd);
        std::fill(ram.begin() + static_cast<std::ptrdiff_t>(rx_off),
                  ram.begin() + static_cast<std::ptrdiff_t>(rx_off + rlen_bytes),
                  static_cast<u8>(0));
        break;
    }

    cursor = rx_off + rlen_bytes;
    return true;
}

int Pif::process_joybus() {
    // Only act when the CPU set the process flag (bit0 of last byte).
    // Some software always leaves the flag set; we still process.
    std::size_t cursor = 0;
    int answered = 0;

    for (std::size_t ch = 0; ch < kChannels; ++ch) {
        const std::size_t before = cursor;
        if (!process_channel(ch, cursor)) {
            break;
        }
        if (cursor != before) {
            ++answered;
        }
        if (cursor >= 0x3F) {
            break;
        }
    }

    // Clear process request bits; leave other control bits alone.
    pif_ram_[0x3F] &= static_cast<u8>(~kCtrlProcess);
    // Set 'done' style bit used by some HLE: bit7 of 0x3F sometimes.
    // Leave as 0 for compatibility with libdragon which polls SI.

    ++joybus_count_;
    N64_TRACE("Joybus processed {} channel slot(s) (count={})", answered, joybus_count_);
    return answered;
}

} // namespace n64
