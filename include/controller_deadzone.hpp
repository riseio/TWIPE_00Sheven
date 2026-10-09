#pragma once

namespace recomp::config { class Config; }
namespace twine::controller {
inline constexpr char deadzone_option[] = "gameplay_stick_deadzone";

inline constexpr double default_deadzone_percent = 24.0;
void register_deadzone(recomp::config::Config& config);
float deadzone();
}
