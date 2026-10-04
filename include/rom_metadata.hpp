#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include "rom_text.hpp"

namespace twine::rom {
struct WeaponDefaults { float damage = 0; uint8_t magazine = 0, reload = 0; };
struct AmmoDefaults { uint16_t pickup = 0, maximum = 0; };
struct Metadata {
    std::array<std::string, static_cast<size_t>(Text::count)> text{};
    std::array<WeaponDefaults, 36> weapons{};
    std::array<AmmoDefaults, 17> ammo{};
};
Metadata read_metadata(std::span<const uint8_t> validated_rom);
void initialize(std::span<const uint8_t> validated_rom);
const Metadata& metadata();
}
