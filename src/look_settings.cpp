#include "look_settings.hpp"
#include "librecomp/config.hpp"
#include "twine_recomp.h"
#include <array>
#include <atomic>
#include <cmath>

namespace twine::look {
namespace {
std::atomic<unsigned> switches{0};
std::array<std::atomic<float>, 2> sensitivities{1.0f, 1.0f};
void add_switch(recomp::config::Config& config, unsigned bit, const char* key,
        const char* name, const char* description, bool initial = false) {
    config.add_bool_option(key, name, description, initial);
    if (initial) switches.fetch_or(bit, std::memory_order_relaxed);
    config.add_option_change_callback(key, [bit](recomp::config::ConfigValueVariant value,
            recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
        if (std::get<bool>(value)) switches.fetch_or(bit, std::memory_order_relaxed);
        else switches.fetch_and(~bit, std::memory_order_relaxed);
    });
}
}
Settings settings() {
    const auto value = switches.load(std::memory_order_relaxed);
    return {bool(value & 1), bool(value & 2), bool(value & 4), bool(value & 8),
        bool(value & 16), bool(value & 32), sensitivities[0].load(std::memory_order_relaxed),
        sensitivities[1].load(std::memory_order_relaxed)};
}
void sync_auto_aim(uint8_t* rdram) {
    const bool enabled = settings().auto_aim;
    for (unsigned player = 0; player < 4; ++player) {
        TWINE_MEM_B(0, 0x80115089U + player * 0xA0U) = enabled ? 1 : 0;
    }
}
void register_settings(recomp::config::Config& config, bool auto_aim_default) {
    switches.store(0, std::memory_order_relaxed);
    add_switch(config, 1, "mouse_acceleration", "Mouse Acceleration",
        "Increases mouse-look speed with faster mouse movement. Off uses a fixed distance per mouse count.");
    add_switch(config, 2, "mouse_invert_x", "Invert Mouse Horizontal Axis", "Reverse horizontal mouse aiming.");
    add_switch(config, 4, "mouse_invert_y", "Invert Mouse Vertical Axis", "Reverse vertical mouse aiming.");
    add_switch(config, 8, "controller_invert_x", "Invert Right Stick Horizontal Axis", "Reverse horizontal controller aiming.");
    add_switch(config, 16, "controller_invert_y", "Invert Right Stick Vertical Axis", "Reverse vertical controller aiming.");
    add_switch(config, 32, "auto_aim", "Auto Aim", "Assist the weapon's aim toward nearby enemies.", auto_aim_default);
    for (unsigned axis = 0; axis < 2; ++axis) {
        sensitivities[axis].store(1.0f, std::memory_order_relaxed);
        const char* key = axis == 0 ? "joystick_sensitivity_x" : "joystick_sensitivity_y";
        config.add_number_option(key, axis == 0 ? "Right Stick Horizontal Sensitivity" : "Right Stick Vertical Sensitivity",
            axis == 0 ? "Horizontal speed for right-stick aiming." : "Vertical speed for right-stick aiming.",
            0, 200, 5, 0, true, 100);
        config.add_option_change_callback(key, [axis](recomp::config::ConfigValueVariant value,
                recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
            const double raw = std::get<double>(value);
            sensitivities[axis].store(std::isfinite(raw) ? float(std::clamp(raw, 0.0, 200.0) / 100.0) : 1.0f,
                std::memory_order_relaxed);
        });
    }
}
}
