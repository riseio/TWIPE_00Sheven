#ifndef TWINE_LOOK_SETTINGS_HPP
#define TWINE_LOOK_SETTINGS_HPP

#include "modern_input.hpp"
namespace recomp::config { class Config; }
namespace twine::look {
struct Settings {
    bool mouse_acceleration = false;
    bool mouse_x_inverted = false;
    bool mouse_y_inverted = false;
    bool stick_x_inverted = false;
    bool stick_y_inverted = false;
    bool auto_aim = false;
    float stick_x_sensitivity = 1.0f;
    float stick_y_sensitivity = 1.0f;
};
void register_settings(recomp::config::Config& config, bool auto_aim_default);
Settings settings();
void sync_auto_aim(uint8_t* rdram);
inline void apply_mouse(modern_input::State& state, float x, float y, const Settings& options) {
    const float gain = modern_input::mouse_gain(x, y, options.mouse_acceleration);
    state.look_right += x * gain * (options.mouse_x_inverted ? -1.0f : 1.0f);
    state.look_up -= y * gain * (options.mouse_y_inverted ? -1.0f : 1.0f);
}
}
#endif
