#pragma once
#include <cstdint>
namespace twine::night_vision_overlay {

void prepare(uint8_t* rdram, uint32_t root, bool mission_ui);
void finish(uint8_t* rdram);
}
