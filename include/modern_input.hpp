#ifndef TWINE_MODERN_INPUT_HPP
#define TWINE_MODERN_INPUT_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace twine::modern_input {

constexpr uint16_t button_a = 0x8000;
constexpr uint16_t button_b = 0x4000;
constexpr uint16_t button_start = 0x1000;
constexpr uint16_t button_l = 0x0020;
constexpr uint16_t button_r = 0x0010;
constexpr uint16_t c_up = 0x0008;
constexpr uint16_t c_down = 0x0004;
constexpr uint16_t c_left = 0x0002;
constexpr uint16_t c_right = 0x0001;
constexpr uint16_t dpad_up = 0x0800;
constexpr uint16_t dpad_down = 0x0400;
constexpr uint16_t dpad_left = 0x0200;
constexpr uint16_t dpad_right = 0x0100;
constexpr float mouse_aim_scale = 0.08f;
constexpr float mouse_acceleration_rate = 0.04f;
constexpr float mouse_acceleration_limit = 2.0f;
constexpr float angle_units_per_turn = 4096.0f;
constexpr float look_angle_scale = 0.04f * 1024.0f;
constexpr float pitch_limit = angle_units_per_turn * 89.0f / 360.0f;
constexpr float pitch_min = -pitch_limit;
constexpr float pitch_max = pitch_limit;
constexpr uint64_t pitch_reset_frame_gap = 10;

constexpr bool gameplay_overlay_active(int16_t overlay) {
    return overlay > 1;
}

inline bool gameplay_mapping_active(
    int16_t overlay,
    uint8_t demo_mode,
    bool native_control_owned
) {
    return gameplay_overlay_active(overlay) && demo_mode == 0 && native_control_owned;
}

constexpr bool native_ladder_look_owned(uint16_t mode, uint8_t ladder_phase) {
    return mode == 1 && (ladder_phase == 2 || ladder_phase == 3);
}

enum class WeaponCategory : uint8_t {
    None,
    Handguns,
    SubmachineGuns,
    Rifles,
    Shotgun,
    Explosives,
    Miscellaneous,
};

enum class Action : uint8_t {
    CycleMode = 1U << 0,
    CycleWeapon = 1U << 1,
    CycleGadget = 1U << 2,
    ToggleLighting = 1U << 3,
    ZoomIn = 1U << 4,
    ZoomOut = 1U << 5,
};

constexpr uint8_t action_bit(Action action) {
    return static_cast<uint8_t>(action);
}

constexpr std::array<uint8_t, 4> handgun_weapons{4, 6, 8, 38};
constexpr std::array<uint8_t, 5> submachine_gun_weapons{9, 10, 11, 13, 14};
constexpr std::array<uint8_t, 6> rifle_weapons{15, 16, 17, 18, 22, 23};
constexpr std::array<uint8_t, 1> shotgun_weapons{20};
constexpr std::array<uint8_t, 6> explosive_weapons{24, 27, 28, 30, 31, 32};
constexpr std::array<uint8_t, 2> miscellaneous_weapons{1, 3};

inline WeaponCategory weapon_category(uint16_t buttons) {
    if ((buttons & c_left) != 0) {
        return WeaponCategory::Handguns;
    }
    if ((buttons & c_right) != 0) {
        return WeaponCategory::SubmachineGuns;
    }
    if ((buttons & dpad_left) != 0) {
        return WeaponCategory::Rifles;
    }
    if ((buttons & dpad_right) != 0) {
        return WeaponCategory::Shotgun;
    }
    if ((buttons & dpad_up) != 0) {
        return WeaponCategory::Explosives;
    }
    if ((buttons & dpad_down) != 0) {
        return WeaponCategory::Miscellaneous;
    }
    return WeaponCategory::None;
}

constexpr uint16_t route_scoped_dpad(
    uint16_t buttons,
    bool scoped,
    bool zoom_requested
) {
    if (!scoped || !zoom_requested) {
        return buttons;
    }

    return static_cast<uint16_t>(buttons & ~(dpad_up | dpad_down));
}

inline uint8_t action_mask(
    uint16_t buttons,
    bool cycle_mode,
    bool toggle_lighting = false,
    bool zoom_in = false,
    bool zoom_out = false
) {
    uint8_t actions = cycle_mode ? action_bit(Action::CycleMode) : 0;
    if ((buttons & button_a) != 0 ||
        weapon_category(buttons) != WeaponCategory::None) {
        actions |= action_bit(Action::CycleWeapon);
    }
    if ((buttons & button_r) != 0) {
        actions |= action_bit(Action::CycleGadget);
    }
    if (toggle_lighting) {
        actions |= action_bit(Action::ToggleLighting);
    }
    if (zoom_in) {
        actions |= action_bit(Action::ZoomIn);
    }
    if (zoom_out) {
        actions |= action_bit(Action::ZoomOut);
    }
    return actions;
}

constexpr uint8_t rising_actions(uint8_t current, uint8_t previous) {
    return current & static_cast<uint8_t>(~previous);
}

inline std::span<const uint8_t> weapons_for_category(
    WeaponCategory category
) {
    switch (category) {
    case WeaponCategory::Handguns:
        return handgun_weapons;
    case WeaponCategory::SubmachineGuns:
        return submachine_gun_weapons;
    case WeaponCategory::Rifles:
        return rifle_weapons;
    case WeaponCategory::Shotgun:
        return shotgun_weapons;
    case WeaponCategory::Explosives:
        return explosive_weapons;
    case WeaponCategory::Miscellaneous:
        return miscellaneous_weapons;
    default:
        return {};
    }
}

inline uint8_t weapon_family(uint8_t weapon) {
    switch (weapon) {
    case 2:
        return 1;
    case 5:
        return 4;
    case 7:
        return 6;
    case 12:
        return 11;
    case 19:
        return 18;
    case 21:
        return 20;
    case 25:
        return 24;
    case 29:
        return 28;
    default:
        return weapon;
    }
}

inline uint8_t next_category_weapon(
    WeaponCategory category,
    uint8_t current,
    uint64_t available_weapons
) {
    const std::span weapons = weapons_for_category(category);
    if (weapons.empty()) {
        return current;
    }

    size_t start = 0;
    const uint8_t current_family = weapon_family(current);
    for (size_t i = 0; i < weapons.size(); ++i) {
        if (weapons[i] == current_family) {
            start = (i + 1) % weapons.size();
            break;
        }
    }
    for (size_t i = 0; i < weapons.size(); ++i) {
        const uint8_t weapon = weapons[(start + i) % weapons.size()];
        if ((available_weapons & (uint64_t{1} << weapon)) != 0) {
            return weapon;
        }
    }
    return current;
}

struct State {
    float forward;
    float strafe_right;
    float look_right;
    float look_up;
};

inline float mouse_gain(float x, float y, bool acceleration) {
    if (!acceleration) {
        return mouse_aim_scale;
    }
    const float acceleration_gain = std::min(
        std::hypot(x, y) * mouse_acceleration_rate,
        mouse_acceleration_limit);
    return mouse_aim_scale * (1.0f + acceleration_gain);
}

inline State compose(
    float move_x,
    float move_y,
    float aim_x,
    float aim_y,
    float mouse_x,
    float mouse_y,
    bool mouse_acceleration,
    float stick_sensitivity_x = 1.0f,
    float stick_sensitivity_y = 1.0f
) {
    const float movement_length = std::hypot(move_x, move_y);
    if (movement_length > 1.0f) {
        move_x /= movement_length;
        move_y /= movement_length;
    }

    const float gain = mouse_gain(mouse_x, mouse_y, mouse_acceleration);
    return {
        move_y,
        move_x,
        aim_x * std::clamp(stick_sensitivity_x, 0.0f, 2.0f) + mouse_x * gain,
        -aim_y * std::clamp(stick_sensitivity_y, 0.0f, 2.0f) - mouse_y * gain,
    };
}

inline float apply_pitch(float pitch, float look_up) {
    return std::clamp(
        pitch - look_up * look_angle_scale,
        pitch_min,
        pitch_max);
}

inline float yaw_delta(float look_right) {
    return -look_right * look_angle_scale;
}

struct PitchState {
    uint32_t owner = 0;
    uint64_t last_frame = 0;
    float value = 0.0f;
    bool initialized = false;
};

inline float update_pitch(
    PitchState& state,
    uint32_t owner,
    uint64_t frame,
    float native_pitch,
    float look_up
) {
    const bool stale =
        state.initialized &&
        (frame < state.last_frame ||
         frame - state.last_frame > pitch_reset_frame_gap);
    if (!state.initialized || state.owner != owner || stale) {
        state.owner = owner;
        state.value = std::clamp(native_pitch, pitch_min, pitch_max);
        state.initialized = true;
    }
    state.last_frame = frame;
    state.value = apply_pitch(state.value, look_up);
    return state.value;
}

inline bool action_axis(
    uint32_t action_a,
    uint32_t action_b,
    const State& state,
    float& value
) {
    if (action_a == 0 && action_b == 1) {
        value = state.forward;
    }
    else if (action_a == 2 && action_b == 3) {
        value = -state.strafe_right;
    }
    else {
        return false;
    }
    return true;
}

inline uint16_t remap_buttons(uint16_t buttons) {
    const bool aim = (buttons & button_l) != 0;
    const bool stand_or_jump = (buttons & c_up) != 0;
    const bool crouch = (buttons & c_down) != 0;
    const bool category =
        weapon_category(buttons) != WeaponCategory::None;

    buttons &= static_cast<uint16_t>(~(
        button_l |
        button_r |
        c_up |
        c_down |
        c_left |
        c_right |
        dpad_up |
        dpad_down |
        dpad_left |
        dpad_right));
    if (aim) {
        buttons |= button_r;
    }
    if (stand_or_jump) {
        buttons |= dpad_up;
    }
    if (crouch) {
        buttons |= dpad_down;
    }
    if (category) {
        buttons |= button_a;
    }
    return buttons;
}

inline uint16_t remap_controller_menu_buttons(
    uint16_t buttons,
    bool semantic_accept = false,
    bool semantic_back = false
) {
    const bool accept = semantic_accept || (buttons & c_up) != 0;
    const bool back = semantic_back || (buttons & c_down) != 0;
    buttons &= static_cast<uint16_t>(~(button_a | button_b | c_up | c_down));
    if (accept) {
        buttons |= button_a;
    }
    if (back) {
        buttons |= button_b;
    }
    return buttons;
}

inline uint16_t remap_keyboard_menu_buttons(
    uint16_t buttons,
    bool semantic_accept,
    bool semantic_back,
    bool native_start_context
) {
    if (semantic_accept && !native_start_context) {

        buttons &= static_cast<uint16_t>(~button_start);
        buttons |= button_a;
    }
    if (semantic_back) {
        buttons |= button_b;
    }
    return buttons;
}

inline uint16_t remap_pause_quit_confirmation_buttons(
    uint16_t buttons,
    bool semantic_accept,
    bool semantic_back,
    bool physical_face_owner = false,
    bool physical_a = false,
    bool physical_b = false
) {

    const bool accept = physical_face_owner
        ? physical_a
        : semantic_accept || (buttons & (button_a | c_up)) != 0;
    const bool back = physical_face_owner
        ? physical_b
        : semantic_back || (buttons & (button_b | c_down)) != 0;
    buttons &= static_cast<uint16_t>(
        ~(button_a | button_b | c_up | c_down | button_start));
    if (accept == back) {
        return buttons;
    }
    return static_cast<uint16_t>(
        buttons | (accept ? button_b : button_a));
}

struct KeyboardMenuAccept {
    bool accept = false;
    bool suppress_start = false;
};

class KeyboardMenuAcceptLatch {
public:
    void observe_gameplay(bool start_down) {
        if (start_down) {
            blocked_until_release_ = true;
        }
    }

    KeyboardMenuAccept update_frontend(
        bool semantic_accept,
        bool start_down,
        bool native_start_context
    ) {
        if (!semantic_accept && !start_down) {
            blocked_until_release_ = false;
        }
        if (native_start_context) {
            if (start_down) {
                blocked_until_release_ = true;
            }
            return {};
        }
        return {
            semantic_accept && !blocked_until_release_,
            start_down && blocked_until_release_};
    }

private:
    bool blocked_until_release_ = false;
};

inline void apply_menu_dpad(uint16_t& buttons, float& x, float& y) {
    if ((buttons & dpad_left) != 0) {
        x = -1.0f;
    }
    else if ((buttons & dpad_right) != 0) {
        x = 1.0f;
    }
    if ((buttons & dpad_up) != 0) {
        y = 1.0f;
    }
    else if ((buttons & dpad_down) != 0) {
        y = -1.0f;
    }

    buttons &= static_cast<uint16_t>(~(
        dpad_left | dpad_right | dpad_up | dpad_down));
}

}

#endif
