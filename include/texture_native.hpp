#pragma once
#include "texture_reconstruction.hpp"
#include "common/rt64_load_types.h"
#include <array>
namespace twine::textures {
struct NativeTile {
    uint64_t hash = 0;
    uint32_t width = 0, height = 0;

    uint32_t tlut = 0;
    RT64::LoadTile tile{};
    std::array<uint8_t, 4096> tmem{};
};
Image decode_native(const NativeTile &);
}
