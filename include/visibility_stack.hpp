#pragma once
#include <cstdint>

namespace twine::render {

inline constexpr uint32_t visibility_stack_size = 0x6000;
void prepare_visibility_stack(uint32_t address);
bool visibility_stack_contains(uint32_t address, uint32_t size);
void reset_visibility_stack();
}
