#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace twine::fonts {
inline constexpr uint32_t width = 128, height = 64, scale = 8;
inline constexpr std::size_t font_count = 5, glyph_count = 96;
struct Glyph { uint8_t x, y, width, height, baseline; };
struct Atlas {
    std::array<uint8_t, width * height / 2> packed{};
    std::array<Glyph, glyph_count> glyphs{};
    uint8_t space_advance = 4;
    int8_t letter_spacing = 1;
};
using Atlases = std::array<Atlas, font_count>;

bool read_atlases(uint8_t* rdram, uint32_t bank, Atlases& out);
std::vector<uint8_t> reconstruct(const Atlas& atlas);
std::vector<uint8_t> encode_dds(std::span<const uint8_t> rgba);
}
