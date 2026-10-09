#include "field_of_view.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <stdexcept>

#include "librecomp/config.hpp"
#include "twine_recomp.h"

namespace twine::fov {
namespace {
std::atomic<float> selected_degrees{default_degrees};
thread_local uint32_t active_camera = 0;
thread_local float frame_degrees = default_degrees;

double bounded(double value) {
    return std::isfinite(value)
        ? std::clamp(value, double(minimum_degrees), double(maximum_degrees))
        : default_degrees;
}
bool valid(uint32_t address, uint32_t size) {
    return !(address & 3U) && address >= 0x80000000U &&
        address < 0x80800000U && size <= 0x80800000U - address;
}
bool player_camera(uint8_t* rdram, uint32_t camera) {
    if (!rdram || !valid(camera, 0x14C)) return false;
    const uint32_t controller = TWINE_MEM_W(0x138, camera);
    if (!valid(controller, 0x70)) return false;
    for (unsigned player = 0; player < 4; ++player) {
        if (uint32_t(TWINE_MEM_W(player * 4, 0x80109688U)) != camera ||
                uint32_t(TWINE_MEM_W(player * 4, 0x8010A500U)) != controller) continue;
        const uint32_t inventory = TWINE_MEM_W(0x6C, controller);
        return valid(inventory, 0x184) && TWINE_MEM_BU(0x182, inventory) == player;
    }
    return false;
}
}

float degrees() { return selected_degrees.load(std::memory_order_relaxed); }

void register_setting(recomp::config::Config& config) {
    selected_degrees.store(default_degrees, std::memory_order_relaxed);
    config.add_number_option(option, "Field of View (Vertical)",
        "Sets the player camera's vertical field of view in degrees. The original is 60. "
        "Ultrawide screens expand the horizontal view. "
        "Higher values widen the lens; very wide views stretch objects near the edges. Scope magnification is preserved; "
        "hands, weapons and scripted cameras keep their authored view.",
        minimum_degrees, maximum_degrees, 1, 0, false, default_degrees);
    config.on_json_parse_option(option, [](const nlohmann::json& value) {
        return recomp::config::ConfigValueVariant{
            value.is_number() ? bounded(value.get<double>()) : double(default_degrees)};
    });
    config.add_option_change_callback(option, [](recomp::config::ConfigValueVariant value,
            recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext context) {
        const float selected = float(bounded(std::get<double>(value)));
        selected_degrees.store(selected, std::memory_order_relaxed);

    });
}

void reset_default(recomp::config::Config& config) {
    config.update_option_value(option, double(default_degrees));
}

void begin(uint8_t* rdram, uint32_t camera) {
    if (active_camera) throw std::logic_error("Nested player camera lens transaction");
    if (!player_camera(rdram, camera)) return;
    active_camera = camera;
    frame_degrees = degrees();
}

uint32_t adjusted(uint8_t* rdram, uint32_t camera, uint32_t original) {
    const float selected = active_camera == camera ? frame_degrees : degrees();
    if (selected == default_degrees || !player_camera(rdram, camera)) return original;
    const float authored = std::bit_cast<float>(original);
    if (!std::isfinite(authored) || authored <= 0 || authored >= 180) return original;

    constexpr double half_radians = std::numbers::pi / 360.0;
    const float effective = authored == default_degrees ? selected : float(
        std::atan(std::tan(authored * half_radians) *
            std::tan(selected * half_radians) / std::tan(default_degrees * half_radians)) /
        half_radians);
    return std::bit_cast<uint32_t>(effective);
}

void finish(uint8_t*) {
    active_camera = 0;
}
}

extern "C" void twine_begin_camera_fov(uint8_t* rdram, uint32_t camera) try {
    twine::fov::begin(rdram, camera);
} catch (const std::exception& error) {
    std::fprintf(stderr, "Camera field of view failed: %s\n", error.what());
    std::abort();
}
extern "C" void twine_finish_camera_fov(uint8_t* rdram) {
    twine::fov::finish(rdram);
}
extern "C" uint32_t twine_camera_fov(uint8_t* rdram, uint32_t camera, uint32_t authored) {
    return twine::fov::adjusted(rdram, camera, authored);
}
