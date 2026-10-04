#include "keyboard_controls.hpp"

#include "recompinput/recompinput.h"
#include "recompinput/profiles.h"

namespace twine::keyboard {
using recompinput::GameInput;
using recompinput::InputField;

void configure_defaults() {
    recompinput::set_default_mapping_for_keyboard(
        GameInput::A, {InputField::keyboard(SDL_SCANCODE_E)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::B, {InputField::keyboard(SDL_SCANCODE_F)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::Z,
        {
            InputField::mouse(SDL_BUTTON_LEFT),
        });
    recompinput::set_default_mapping_for_keyboard(
        GameInput::L,
        {InputField::mouse(SDL_BUTTON_RIGHT)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::R, {InputField::keyboard(SDL_SCANCODE_Q)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::SPRINT, {InputField::keyboard(SDL_SCANCODE_LSHIFT)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::RELOAD, {InputField::keyboard(SDL_SCANCODE_R)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::CYCLE_MODE, {InputField::keyboard(SDL_SCANCODE_V)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::ZOOM_IN,
        {
            InputField::keyboard(SDL_SCANCODE_Z),
            InputField::mouse(SDL_BUTTON_X1),
        });
    recompinput::set_default_mapping_for_keyboard(
        GameInput::ZOOM_OUT,
        {
            InputField::keyboard(SDL_SCANCODE_X),
            InputField::mouse(SDL_BUTTON_X2),
        });
    recompinput::set_default_mapping_for_keyboard(
        GameInput::TOGGLE_LIGHTING, {InputField::keyboard(SDL_SCANCODE_GRAVE)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::SAVE_STATE, {InputField::keyboard(SDL_SCANCODE_F5)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::RESTORE_STATE, {InputField::keyboard(SDL_SCANCODE_F8)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::TOGGLE_XRAY, {InputField::keyboard(SDL_SCANCODE_T)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::OBJECTIVES, {InputField::keyboard(SDL_SCANCODE_TAB)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::C_UP, {InputField::keyboard(SDL_SCANCODE_SPACE)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::C_DOWN, {InputField::keyboard(SDL_SCANCODE_LCTRL)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::C_LEFT, {InputField::keyboard(SDL_SCANCODE_1)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::C_RIGHT, {InputField::keyboard(SDL_SCANCODE_2)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::DPAD_LEFT, {InputField::keyboard(SDL_SCANCODE_3)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::DPAD_RIGHT, {InputField::keyboard(SDL_SCANCODE_4)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::DPAD_UP, {InputField::keyboard(SDL_SCANCODE_5)});
    recompinput::set_default_mapping_for_keyboard(
        GameInput::DPAD_DOWN, {InputField::keyboard(SDL_SCANCODE_6)});

}

bool migrate_defaults() {
    namespace profiles = recompinput::profiles;
    bool changed = false;
    for (int profile = 0; profile < profiles::get_input_profile_count(); ++profile) {
        if (profiles::get_input_profile_device(profile) != recompinput::InputDevice::Keyboard ||
                profiles::is_input_profile_custom(profile)) { continue; }
        const auto matches = [&](GameInput input, InputField first, InputField second = {}) {
            return profiles::get_input_binding(profile, input, 0) == first &&
                profiles::get_input_binding(profile, input, 1) == second;
        };
        const auto replace = [&](GameInput input, InputField first) {
            profiles::clear_input_binding(profile, input);
            profiles::set_input_binding(profile, input, 0, first);
            changed = true;
        };
        const auto key = InputField::keyboard;
        const auto migrate_key = [&](GameInput input, SDL_Scancode from, SDL_Scancode to) {
            if (matches(input, key(from))) { replace(input, key(to)); }
        };
        migrate_key(GameInput::TOGGLE_LIGHTING, SDL_SCANCODE_F6, SDL_SCANCODE_GRAVE);
        if (matches(GameInput::L, key(SDL_SCANCODE_R), InputField::mouse(SDL_BUTTON_RIGHT))) {
            replace(GameInput::L, InputField::mouse(SDL_BUTTON_RIGHT));
        }

        if (matches(GameInput::A, key(SDL_SCANCODE_Q)) &&
                matches(GameInput::B, key(SDL_SCANCODE_E)) &&
                (matches(GameInput::R, key(SDL_SCANCODE_G)) ||
                 matches(GameInput::R, key(SDL_SCANCODE_LSHIFT))) &&
                matches(GameInput::Z, key(SDL_SCANCODE_F), InputField::mouse(SDL_BUTTON_LEFT))) {
            replace(GameInput::A, key(SDL_SCANCODE_E));
            replace(GameInput::B, key(SDL_SCANCODE_F));
            replace(GameInput::R, key(SDL_SCANCODE_Q));
            replace(GameInput::Z, InputField::mouse(SDL_BUTTON_LEFT));
        }
        migrate_key(GameInput::BACK_MENU, SDL_SCANCODE_F15, SDL_SCANCODE_BACKSPACE);
        migrate_key(GameInput::TAB_LEFT_MENU, SDL_SCANCODE_F16, SDL_SCANCODE_PAGEUP);
        migrate_key(GameInput::TAB_RIGHT_MENU, SDL_SCANCODE_F17, SDL_SCANCODE_PAGEDOWN);
    }
    return changed;
}
}
