#pragma once

#include <cstdint>
#include <span>

namespace twine::render {
struct Command { uint32_t w0, w1; };
uint32_t ui_identity(uint32_t node, uint32_t root, uint32_t resource);

void prefix_command(uint8_t* rdram, uint32_t address, std::span<const Command> prefix);
void wrap_commands(uint8_t* rdram, uint32_t begin, uint32_t end,
    std::span<const Command> prefix, std::span<const Command> suffix);
}
