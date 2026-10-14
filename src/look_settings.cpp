#include "look_settings.hpp"
#include "librecomp/config.hpp"
#include "twine_recomp.h"
#include "sniper_controls.hpp"
#include "rom_metadata.hpp"
#include <bit>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace {
recomp::config::Config* look_config = nullptr;
bool applying_native_option = false;
}

extern "C" uint32_t twine_auto_aim_active(uint8_t* rdram, uint32_t player) {
    if (!rdram || !twine::look::settings().auto_aim ||
            TWINE_MEM_BU(0, 0x800CEEB0U) || TWINE_MEM_H(0, 0x80117386U) <= 1) { return 0; }
    if ((player & 3U) || player < 0x80000000U || player > 0x807FFF90U) { return 0; }
    const uint32_t item = TWINE_MEM_W(0x68, player);
    if ((item & 3U) || item < 0x80000000U || item > 0x807FFFF0U) { return 0; }
    const uint8_t weapon = TWINE_MEM_BU(0xE, item);
    return weapon > 0 && weapon < 32 && !twine::sniper::owns_scope_action(weapon);
}

extern "C" uint32_t twine_auto_aim_range(uint8_t* rdram, uint32_t player, uint32_t native_range) {

    if (std::bit_cast<float>(native_range) == 0 && twine_auto_aim_active(rdram, player) &&
            TWINE_MEM_BU(0, 0x80102EE8U) == 2 && TWINE_MEM_W(0, 0x80102ED8U) == 0) {
        return std::bit_cast<uint32_t>(twine::rom::metadata().auto_aim_range);
    }
    return native_range;
}

extern "C" void twine_adjust_auto_aim_window(uint8_t* rdram, recomp_context* ctx) {

    if (twine::look::settings().reduced_auto_aim_area && twine_auto_aim_active(rdram, (uint32_t)ctx->r19)) {
        ctx->f28.fl *= 2.0f;
        ctx->f22.fl *= 2.0f;
    }
}

namespace twine::look {
namespace {
std::atomic<unsigned> switches{0};
constexpr unsigned auto_aim_bit = 32, native_edit_bit = 64;
std::array<std::atomic<float>, 2> sensitivities{1.0f, 1.0f};
std::atomic<bool> reduced_auto_aim_area{false};
void set_auto_aim(bool enabled, bool native_edit) {
    unsigned before = switches.load(std::memory_order_relaxed);
    unsigned after;
    do {
        after = before & ~(auto_aim_bit | native_edit_bit);
        if (enabled) after |= auto_aim_bit;
        if (native_edit) after |= native_edit_bit;
    } while (!switches.compare_exchange_weak(before, after, std::memory_order_relaxed));
}
void add_switch(recomp::config::Config& config, unsigned bit, const char* key,
        const char* name, const char* description, bool initial = false) {
    config.add_bool_option(key, name, description, initial);
    if (initial) switches.fetch_or(bit, std::memory_order_relaxed);
    config.add_option_change_callback(key, [bit](recomp::config::ConfigValueVariant value,
            recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
        if (bit == auto_aim_bit) {
            if (!applying_native_option) set_auto_aim(std::get<bool>(value), false);
            return;
        }
        if (std::get<bool>(value)) switches.fetch_or(bit, std::memory_order_relaxed);
        else switches.fetch_and(~bit, std::memory_order_relaxed);
    });
}
}
Settings settings() {
    const auto value = switches.load(std::memory_order_relaxed);
    return {bool(value & 1), bool(value & 2), bool(value & 4), bool(value & 8),
        bool(value & 16), bool(value & 32), sensitivities[0].load(std::memory_order_relaxed),
        sensitivities[1].load(std::memory_order_relaxed), reduced_auto_aim_area.load(std::memory_order_relaxed)};
}
void update_ui() {
    if (!look_config) return;
    const unsigned edit = switches.fetch_and(~native_edit_bit, std::memory_order_relaxed);
    if (!(edit & native_edit_bit)) return;
    const bool changed = edit & auto_aim_bit;

    struct NativeApply {
        NativeApply() { applying_native_option = true; }
        ~NativeApply() { applying_native_option = false; }
    } guard;
    look_config->update_option_value("auto_aim", changed);
    look_config->apply_option_value("auto_aim");
    if (!look_config->save_applied_config()) {
        std::fprintf(stderr, "Could not save Auto Aim preference to general.json\n");
    }
}
void sync_auto_aim(uint8_t* rdram) {
    const bool enabled = settings().auto_aim;
    for (unsigned player = 0; player < 4; ++player) {
        TWINE_MEM_B(0, 0x80115089U + player * 0xA0U) = enabled ? 1 : 0;
    }
}
void register_settings(recomp::config::Config& config, bool auto_aim_default) {
    look_config = &config;
    switches.store(0, std::memory_order_relaxed);
    add_switch(config, 1, "mouse_acceleration", "Mouse Acceleration",
        "Increases mouse-look speed with faster mouse movement. Off uses a fixed distance per mouse count.");
    add_switch(config, 2, "mouse_invert_x", "Invert Mouse Horizontal Axis", "Reverse horizontal mouse aiming.");
    add_switch(config, 4, "mouse_invert_y", "Invert Mouse Vertical Axis", "Reverse vertical mouse aiming.");
    add_switch(config, 8, "controller_invert_x", "Invert Right Stick Horizontal Axis", "Reverse horizontal controller aiming.");
    add_switch(config, 16, "controller_invert_y", "Invert Right Stick Vertical Axis", "Reverse vertical controller aiming.");
    add_switch(config, 32, "auto_aim", "Auto Aim", "Assist the weapon's aim toward nearby enemies.", auto_aim_default);
    reduced_auto_aim_area.store(false, std::memory_order_relaxed);
    config.add_enum_option("auto_aim_area", "Auto Aim Area",
        "Reduced requires targets to be twice as close to your aiming point horizontally and vertically. Full uses the normal area. Requires Auto Aim.",
        {{0U, "Reduced", "Reduced"}, {1U, "Full", "Full"}}, 1U);
    config.add_option_change_callback("auto_aim_area", [](recomp::config::ConfigValueVariant value,
            recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
        reduced_auto_aim_area.store(std::get<uint32_t>(value) == 0U, std::memory_order_relaxed);
    });
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

extern "C" void twine_commit_native_auto_aim(uint32_t player, uint32_t enabled) {
    if (player >= 4 || enabled > 1) return;

    twine::look::set_auto_aim(enabled != 0, true);
}
