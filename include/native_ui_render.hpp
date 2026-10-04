#pragma once
#include <cstdint>

namespace twine::render::ui {
void begin(uint8_t* rdram, uint32_t root, bool mission, bool frontend, bool expanded_hud = false);
}
