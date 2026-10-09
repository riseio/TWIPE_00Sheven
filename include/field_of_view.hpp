#pragma once

#include <cstdint>

namespace recomp::config { class Config; }
namespace recompui { class ContextId; class Element; }

namespace twine::fov {
inline constexpr char option[] = "field_of_view";
inline constexpr float default_degrees = 60.0f;
inline constexpr float minimum_degrees = 40.0f;
inline constexpr float maximum_degrees = 100.0f;

void register_setting(recomp::config::Config& config);
void reset_default(recomp::config::Config& config);
void create_controls(recompui::ContextId context, recompui::Element* parent);
float degrees();

void begin(uint8_t* rdram, uint32_t camera);
void finish(uint8_t* rdram);
}
