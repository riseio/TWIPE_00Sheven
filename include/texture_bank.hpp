#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>
#include <vector>
#include "texture_native.hpp"

namespace twine::textures {

struct BankImage {
    unsigned bank, index, frame, width, height;
    uint16_t flags;
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> palette;
    std::vector<std::vector<uint8_t>> alternate_palettes;
    bool material = false;
};
void visit_bank_images(std::span<const uint8_t> rom, const std::function<void(BankImage &&)> &consume,
                       std::stop_token stop = {});
void visit_bank_tiles(std::span<const uint8_t> rom, const std::function<void(NativeTile &&)> &consume,
                      std::stop_token stop = {});
}
