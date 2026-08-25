#include <gtest/gtest.h>

#include "n64/frontend/input.hpp"

#include <SDL.h>

using namespace n64;
using namespace n64::frontend;

namespace {

SDL_ControllerAxisEvent axis_event(SDL_GameControllerAxis axis, s16 value) {
    SDL_ControllerAxisEvent event{};
    event.axis = static_cast<Uint8>(axis);
    event.value = value;
    return event;
}

SDL_ControllerButtonEvent button_event(SDL_GameControllerButton button, bool down) {
    SDL_ControllerButtonEvent event{};
    event.button = static_cast<Uint8>(button);
    event.state = down ? SDL_PRESSED : SDL_RELEASED;
    return event;
}

} // namespace

TEST(InputMapper, FullNegativeVerticalAxisMapsUp) {
    InputMapper input;
    const auto event = axis_event(SDL_CONTROLLER_AXIS_LEFTY, -32768);
    input.on_controller_axis(event);
    EXPECT_GT(input.state().stick_y, 0);
}

TEST(InputMapper, ReleasingOneTriggerKeepsZWhileOtherIsHeld) {
    InputMapper input;
    auto left = axis_event(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 20000);
    auto right = axis_event(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 20000);
    input.on_controller_axis(left);
    input.on_controller_axis(right);
    EXPECT_TRUE(input.state().button(Button::Z));

    left.value = 0;
    input.on_controller_axis(left);
    EXPECT_TRUE(input.state().button(Button::Z));

    right.value = 0;
    input.on_controller_axis(right);
    EXPECT_FALSE(input.state().button(Button::Z));
}

TEST(InputMapper, AlternateDigitalButtonsDoNotCancelEachOther) {
    InputMapper input;
    input.on_controller_button(button_event(SDL_CONTROLLER_BUTTON_A, true));
    input.on_controller_button(button_event(SDL_CONTROLLER_BUTTON_Y, true));
    input.on_controller_button(button_event(SDL_CONTROLLER_BUTTON_A, false));
    EXPECT_TRUE(input.state().button(Button::A));
    input.on_controller_button(button_event(SDL_CONTROLLER_BUTTON_Y, false));
    EXPECT_FALSE(input.state().button(Button::A));
}

TEST(InputMapper, DigitalUpdatePreservesRightStickCButton) {
    InputMapper input;
    input.on_controller_axis(axis_event(SDL_CONTROLLER_AXIS_RIGHTX, 20000));
    input.on_controller_button(button_event(SDL_CONTROLLER_BUTTON_A, true));
    const ControllerState state = input.state();
    EXPECT_TRUE(state.button(Button::A));
    EXPECT_TRUE(state.button(Button::CRight));
}
