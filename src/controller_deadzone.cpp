#include "controller_deadzone.hpp"
#include "librecomp/config.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace twine::controller {
namespace {
std::atomic<float> selected{float(default_deadzone_percent / 100.0)};
double bounded(double value) {
    return std::isfinite(value) ? std::clamp(value, 0.0, 100.0) : default_deadzone_percent;
}
}
float deadzone() { return selected.load(std::memory_order_relaxed); }
void register_deadzone(recomp::config::Config& config) {
    selected.store(float(default_deadzone_percent / 100.0), std::memory_order_relaxed);
    config.add_number_option(deadzone_option, "Gameplay Stick Deadzone",
        "Prevents unwanted movement and aiming when a stick is released. "
        "Sets the minimum neutral area for both sticks during gameplay. "
        "Higher values ignore more movement near the center.",
        0, 100, 1, 0, true, default_deadzone_percent);
    config.on_json_parse_option(deadzone_option, [](const nlohmann::json& value) {
        return recomp::config::ConfigValueVariant{value.is_number()
            ? bounded(value.get<double>()) : default_deadzone_percent};
    });
    config.add_option_change_callback(deadzone_option, [](recomp::config::ConfigValueVariant value,
            recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
        selected.store(float(bounded(std::get<double>(value)) / 100.0), std::memory_order_relaxed);
    });
}
}
