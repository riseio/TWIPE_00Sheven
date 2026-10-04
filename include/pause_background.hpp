#pragma once

#include <cstdint>

namespace twine::pause_background {

void prepare(uint8_t* rdram, uint32_t root);
void finish(uint8_t* rdram);
}
