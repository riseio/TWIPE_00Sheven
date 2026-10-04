#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <span>

namespace twine::textures {
using MaterialPairs = std::map<unsigned, std::set<unsigned>>;

void collect_material_pairs(std::span<uint8_t> arena, std::span<const uint8_t> commands,
    std::span<const uint8_t> images, std::span<const uint8_t> palettes, MaterialPairs& result);
}
