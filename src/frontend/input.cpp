#include "n64/frontend/input.hpp"

#include "n64/common/log.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>

namespace n64::frontend {
namespace {

s8 clamp_stick(int v) noexcept {
    if (v > 127) return 127;
    if (v < -128) return static_cast<s8>(-128);
    return static_cast<s8>(v);
}

} // namespace

InputMapper::~InputMapper() {
    shutdown();
}

void InputMapper::init() {
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            N64_WARN("SDL_INIT_GAMECONTROLLER failed: {}", SDL_GetError());
            return;
        }
    }
    const int n = SDL_NumJoysticks();
    for (int i = 0; i < n; ++i) {
        if (SDL_IsGameController(i)) {
            pad_ = SDL_GameControllerOpen(i);
            if (pad_) {
                N64_INFO("Game controller opened: {}", SDL_GameControllerName(pad_));
                break;
            }
        }
    }
}

void InputMapper::shutdown() {
    if (pad_) {
        SDL_GameControllerClose(pad_);
        pad_ = nullptr;
    }
}

void InputMapper::set_key_button(Button b, bool down) noexcept {
    if (down) {
        key_buttons_ = static_cast<u16>(key_buttons_ | static_cast<u16>(b));
    } else {
        key_buttons_ = static_cast<u16>(key_buttons_ & ~static_cast<u16>(b));
    }
}

void InputMapper::recompute_key_stick() noexcept {
    int x = 0, y = 0;
    if (key_d_) x += 80;
    if (key_a_) x -= 80;
    if (key_w_) y += 80; // N64 stick Y+ is up
    if (key_s_) y -= 80;
    key_stick_x_ = clamp_stick(x);
    key_stick_y_ = clamp_stick(y);
}

void InputMapper::on_key(const SDL_KeyboardEvent& e) {
    const bool down = (e.state == SDL_PRESSED);
    // Ignore key repeats.
    if (e.repeat) {
        return;
    }

    switch (e.keysym.scancode) {
    case SDL_SCANCODE_Z:      set_key_button(Button::A, down); break;
    case SDL_SCANCODE_X:      set_key_button(Button::B, down); break;
    case SDL_SCANCODE_RETURN: set_key_button(Button::Start, down); break;
    case SDL_SCANCODE_C:      set_key_button(Button::Z, down); break;
    case SDL_SCANCODE_A:
        // A is both L-shoulder chord and left stick — use Q for L instead.
        break;
    case SDL_SCANCODE_Q:      set_key_button(Button::L, down); break;
    case SDL_SCANCODE_E:      set_key_button(Button::R, down); break;

    case SDL_SCANCODE_UP:     set_key_button(Button::DUp, down); break;
    case SDL_SCANCODE_DOWN:   set_key_button(Button::DDown, down); break;
    case SDL_SCANCODE_LEFT:   set_key_button(Button::DLeft, down); break;
    case SDL_SCANCODE_RIGHT:  set_key_button(Button::DRight, down); break;

    case SDL_SCANCODE_I:      set_key_button(Button::CUp, down); break;
    case SDL_SCANCODE_K:      set_key_button(Button::CDown, down); break;
    case SDL_SCANCODE_J:      set_key_button(Button::CLeft, down); break;
    case SDL_SCANCODE_L:      set_key_button(Button::CRight, down); break;

    case SDL_SCANCODE_W: key_w_ = down; recompute_key_stick(); break;
    case SDL_SCANCODE_S: key_s_ = down; recompute_key_stick(); break;
    // A/D for stick — but A key also used above; use A/D exclusively for stick.
    case SDL_SCANCODE_D: key_d_ = down; recompute_key_stick(); break;
    // Left stick X− : use A scancode for stick left (L shoulder is Q).
    // Re-bind: scancode A → stick left
    // (handled below by fallthrough fix)
    default:
        break;
    }

    // Stick left on scancode A (L is Q).
    if (e.keysym.scancode == SDL_SCANCODE_A) {
        key_a_ = down;
        recompute_key_stick();
    }
}

s8 InputMapper::axis_to_stick(int axis) noexcept {
    // SDL axis -32768..32767 → N64 roughly -80..80 (libultra range).
    constexpr int dead = 8000;
    if (axis > -dead && axis < dead) {
        return 0;
    }
    const int scaled = (axis * 80) / 32767;
    return clamp_stick(scaled);
}

void InputMapper::recompute_pad_buttons() noexcept {
    const auto held = [this](int button) noexcept {
        return button >= 0 && static_cast<std::size_t>(button) < pad_button_down_.size() &&
               pad_button_down_[static_cast<std::size_t>(button)];
    };
    u16 buttons = 0;
    const auto set_if = [&buttons](Button button, bool down) noexcept {
        if (down) {
            buttons = static_cast<u16>(buttons | static_cast<u16>(button));
        }
    };
    set_if(Button::A, held(SDL_CONTROLLER_BUTTON_A) || held(SDL_CONTROLLER_BUTTON_Y));
    set_if(Button::B, held(SDL_CONTROLLER_BUTTON_B) || held(SDL_CONTROLLER_BUTTON_X));
    set_if(Button::Start, held(SDL_CONTROLLER_BUTTON_START));
    set_if(Button::Z, held(SDL_CONTROLLER_BUTTON_BACK) ||
                      held(SDL_CONTROLLER_BUTTON_RIGHTSTICK) ||
                      pad_trigger_left_ || pad_trigger_right_);
    set_if(Button::L, held(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
    set_if(Button::R, held(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
    set_if(Button::DUp, held(SDL_CONTROLLER_BUTTON_DPAD_UP));
    set_if(Button::DDown, held(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
    set_if(Button::DLeft, held(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
    set_if(Button::DRight, held(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
    pad_button_mask_ = buttons;
}

void InputMapper::on_controller_button(const SDL_ControllerButtonEvent& e) {
    const bool down = (e.state == SDL_PRESSED);
    if (static_cast<std::size_t>(e.button) >= pad_button_down_.size()) {
        return;
    }
    pad_button_down_[e.button] = down;
    recompute_pad_buttons();
}

void InputMapper::on_controller_axis(const SDL_ControllerAxisEvent& e) {
    switch (e.axis) {
    case SDL_CONTROLLER_AXIS_LEFTX:
        pad_stick_x_ = axis_to_stick(e.value);
        break;
    case SDL_CONTROLLER_AXIS_LEFTY:
        // SDL Y+ is down; N64 Y+ is up.
        pad_stick_y_ = axis_to_stick(-static_cast<int>(e.value));
        break;
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        pad_trigger_right_ = e.value > 16000;
        recompute_pad_buttons();
        break;
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        // Treat half-press as Z.
        pad_trigger_left_ = e.value > 16000;
        recompute_pad_buttons();
        break;
    case SDL_CONTROLLER_AXIS_RIGHTX:
        // Map right stick to C-buttons (threshold).
        pad_axis_mask_ = static_cast<u16>(pad_axis_mask_ &
            ~static_cast<u16>(Button::CLeft | Button::CRight));
        if (e.value > 16000) {
            pad_axis_mask_ = static_cast<u16>(pad_axis_mask_ | static_cast<u16>(Button::CRight));
        } else if (e.value < -16000) {
            pad_axis_mask_ = static_cast<u16>(pad_axis_mask_ | static_cast<u16>(Button::CLeft));
        }
        break;
    case SDL_CONTROLLER_AXIS_RIGHTY:
        pad_axis_mask_ = static_cast<u16>(pad_axis_mask_ &
            ~static_cast<u16>(Button::CUp | Button::CDown));
        if (e.value < -16000) {
            pad_axis_mask_ = static_cast<u16>(pad_axis_mask_ | static_cast<u16>(Button::CUp));
        } else if (e.value > 16000) {
            pad_axis_mask_ = static_cast<u16>(pad_axis_mask_ | static_cast<u16>(Button::CDown));
        }
        break;
    default:
        break;
    }
}

ControllerState InputMapper::state() const noexcept {
    ControllerState s;
    s.present = true;
    s.buttons = static_cast<u16>(key_buttons_ | pad_button_mask_ | pad_axis_mask_);
    // Prefer gamepad stick if deflected, else keyboard.
    if (pad_stick_x_ != 0 || pad_stick_y_ != 0) {
        s.stick_x = pad_stick_x_;
        s.stick_y = pad_stick_y_;
    } else {
        s.stick_x = key_stick_x_;
        s.stick_y = key_stick_y_;
    }
    return s;
}

} // namespace n64::frontend
