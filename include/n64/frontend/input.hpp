#pragma once

#include "n64/pif/pif.hpp"

#include <array>
#include <cstdint>

// Forward-declare SDL types so headers stay light.
struct SDL_KeyboardEvent;
struct SDL_ControllerButtonEvent;
struct SDL_ControllerAxisEvent;
struct _SDL_GameController;
using SDL_GameController = _SDL_GameController;

namespace n64::frontend {

/// Keyboard + optional SDL gamecontroller → N64 ControllerState (port 0).
///
/// Default keyboard map (QWERTY):
///   A=Z  B=X  Start=Enter  Z=c  L=a  R=s
///   D-pad = arrows
///   C-buttons = IJKL (up/left/down/right)
///   Analog = WASD
class InputMapper {
public:
    InputMapper() = default;
    ~InputMapper();

    InputMapper(const InputMapper&) = delete;
    InputMapper& operator=(const InputMapper&) = delete;

    /// Open the first available SDL gamecontroller, if any.
    void init();
    void shutdown();

    void on_key(const SDL_KeyboardEvent& e);
    void on_controller_button(const SDL_ControllerButtonEvent& e);
    void on_controller_axis(const SDL_ControllerAxisEvent& e);

    /// Build the current N64 pad state (keyboard OR'd with gamepad).
    [[nodiscard]] ControllerState state() const noexcept;

    /// Apply directly onto emulator PIF channel 0.
    void apply_to(Pif& pif) const { pif.set_controller0(state()); }

private:
    void set_key_button(Button b, bool down) noexcept;
    static s8 axis_to_stick(int axis) noexcept;
    void recompute_pad_buttons() noexcept;

    // Keyboard-held buttons bitmask + stick from WASD.
    u16 key_buttons_ = 0;
    s8 key_stick_x_ = 0;
    s8 key_stick_y_ = 0;
    bool key_w_ = false, key_a_ = false, key_s_ = false, key_d_ = false;
    void recompute_key_stick() noexcept;

    // Gamepad
    u16 pad_button_mask_ = 0;
    u16 pad_axis_mask_ = 0;
    s8 pad_stick_x_ = 0;
    s8 pad_stick_y_ = 0;
    std::array<bool, 32> pad_button_down_{};
    bool pad_trigger_left_ = false;
    bool pad_trigger_right_ = false;
    SDL_GameController* pad_ = nullptr;
};

} // namespace n64::frontend
