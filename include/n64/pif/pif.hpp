#pragma once

#include "n64/common/types.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace n64 {

/// Controller button bitmask (standard N64 controller, big-endian joybus layout).
enum class Button : u16 {
    CRight = 0x0001,
    CLeft  = 0x0002,
    CDown  = 0x0004,
    CUp    = 0x0008,
    R      = 0x0010,
    L      = 0x0020,
    // 0x0040 reserved
    // 0x0080 reserved / reset
    DRight = 0x0100,
    DLeft  = 0x0200,
    DDown  = 0x0400,
    DUp    = 0x0800,
    Start  = 0x1000,
    Z      = 0x2000,
    B      = 0x4000,
    A      = 0x8000,
};

[[nodiscard]] constexpr Button operator|(Button a, Button b) noexcept {
    return static_cast<Button>(static_cast<u16>(a) | static_cast<u16>(b));
}
[[nodiscard]] constexpr Button operator&(Button a, Button b) noexcept {
    return static_cast<Button>(static_cast<u16>(a) & static_cast<u16>(b));
}
[[nodiscard]] constexpr Button& operator|=(Button& a, Button b) noexcept {
    a = a | b;
    return a;
}

struct ControllerState {
    u16 buttons = 0;
    s8 stick_x = 0;
    s8 stick_y = 0;
    bool present = true;

    void set_button(Button b, bool down) noexcept {
        if (down) {
            buttons = static_cast<u16>(buttons | static_cast<u16>(b));
        } else {
            buttons = static_cast<u16>(buttons & ~static_cast<u16>(b));
        }
    }

    [[nodiscard]] bool button(Button b) const noexcept {
        return (buttons & static_cast<u16>(b)) != 0;
    }
};

/// Joybus command IDs (byte 0 of tx payload).
namespace JoybusCmd {
    inline constexpr u8 Info       = 0x00;
    inline constexpr u8 Controller = 0x01;
    inline constexpr u8 ReadMemPak = 0x02;
    inline constexpr u8 WriteMemPak= 0x03;
    inline constexpr u8 ReadEeprom = 0x04;
    inline constexpr u8 WriteEeprom= 0x05;
    inline constexpr u8 Reset      = 0xFF;
}

/// Standard controller identity returned by Info/Reset.
inline constexpr u16 kControllerType = 0x0500;
inline constexpr u8  kControllerStatus = 0x00; // no pak

/// PIF / SI side: boot + joybus channels + controller state.
class Pif {
public:
    static constexpr std::size_t kChannels = 4;
    static constexpr std::size_t kRamSize = kPifRamSize; // 64
    static constexpr u8 kCtrlProcess = 0x01; // PIF_RAM[0x3F] bit0: run joybus

    void reset();

    void set_controller(std::size_t channel, const ControllerState& state);
    [[nodiscard]] ControllerState controller(std::size_t channel) const;

    /// Update buttons/stick for channel 0 (frontend convenience).
    void set_controller0(const ControllerState& state) { set_controller(0, state); }

    [[nodiscard]] std::span<u8> ram() noexcept { return pif_ram_; }
    [[nodiscard]] std::span<const u8> ram() const noexcept { return pif_ram_; }

    /// Parse joybus command blocks in PIF RAM and fill responses.
    /// Clears the process flag in byte 0x3F when done.
    /// Returns number of channels successfully answered.
    int process_joybus();

    /// True if PIF_RAM[0x3F] requests processing.
    [[nodiscard]] bool process_requested() const noexcept {
        return (pif_ram_[0x3F] & kCtrlProcess) != 0;
    }

    [[nodiscard]] u32 joybus_process_count() const noexcept { return joybus_count_; }

private:
    /// Process one channel starting at `cursor`. Advances cursor past the block.
    /// Returns false if the terminator was hit or the buffer is exhausted.
    bool process_channel(std::size_t channel, std::size_t& cursor);

    std::array<ControllerState, kChannels> controllers_{};
    std::array<u8, kPifRamSize> pif_ram_{};
    u32 joybus_count_ = 0;
};

} // namespace n64
