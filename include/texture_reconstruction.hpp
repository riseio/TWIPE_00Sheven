#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace twine::textures {
struct Image { uint32_t width{}, height{}; std::vector<uint8_t> rgba; };

Image reconstruct(const Image& source, uint32_t scale = 9);

Image reconstruct_material(const Image& source);
std::vector<uint8_t> make_dds(const Image& image);
}
