#include "rom_metadata.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>

namespace twine::rom {
namespace {
Metadata current;
bool initialized = false;
class Reader {
    std::span<const uint8_t> rom;
public:
    explicit Reader(std::span<const uint8_t> bytes) : rom(bytes) {
        if (rom.size() != 33554432) throw std::runtime_error("Unsupported ROM metadata extent");
    }
    uint8_t byte(uint32_t address) const {
        if (address < 0x80000400U || address >= 0x800D7050U)
            throw std::runtime_error("ROM metadata is outside the resident section");
        return rom[address - 0x80000400U + 0x1000U];
    }
    uint16_t half(uint32_t address) const { return uint16_t(uint32_t(byte(address))*256 + byte(address+1)); }
    uint32_t word(uint32_t address) const { return (uint32_t(half(address)) << 16) | half(address+2); }

};
}
Metadata read_metadata(std::span<const uint8_t> validated_rom) {
    Reader source(validated_rom);
    Metadata result;

    constexpr std::array<std::array<uint32_t, 2>, static_cast<size_t>(Text::count)> spans{{
        {0x1499926U, 17U},
        {0x1499926U, 16U},
        {0x1499956U, 20U},
        {0x14998c6U, 22U},
        {0x14998faU, 25U},
        {0x149b95bU, 20U},
        {0x149c01bU, 23U},
        {0x14ad40eU, 25U},
        {0x14acdbeU, 24U},
        {0x14bfa5bU, 21U},
        {0x149c14bU, 11U},
        {0x149c164U, 13U},
        {0x149be10U, 13U},
        {0x149c0bbU, 14U},
        {0x14aca34U, 13U},
        {0x14aa617U, 13U},
        {0x14ad457U, 12U},
        {0x14ad064U, 15U},
        {0x14accccU, 14U},
        {0x14befacU, 11U},
        {0x14befbcU, 10U},
        {0x14bf360U, 13U},
        {0x14bf609U, 14U},
        {0x14998c6U, 15U},
        {0x149c05bU, 17U},
        {0x14998faU, 17U},
        {0x149c1c0U, 18U},
        {0x1498fa0U, 8U},
        {0x1498f8dU, 8U},
        {0x149b911U, 8U},
        {0x149b946U, 8U},
        {0x149b8f2U, 8U},
        {0x14ac9c0U, 8U},
        {0x14a9bc4U, 8U},
        {0x14acda2U, 8U},
        {0x14ad01bU, 8U},
        {0x14a9be7U, 8U},
        {0x14ac9e3U, 8U},
        {0x14ac8a2U, 8U},
        {0x14ac963U, 8U},
        {0x14ac878U, 8U},
        {0x14bc184U, 7U},
        {0x14bc168U, 7U},
        {0x14beefeU, 7U},
        {0x14bef2fU, 7U},
        {0x14beebeU, 7U},
        {0x14a5246U, 5U},
        {0x149c309U, 9U},
        {0x14ad40eU, 9U},
        {0x14bcc77U, 8U},
        {0x14998c6U, 12U},
        {0x149bffdU, 13U},
        {0x1498f8eU, 7U},
        {0x1499926U, 4U},
        {0x149c164U, 6U},
        {0x1499956U, 6U},
        {0x149c0bbU, 7U},
        {0x14998c6U, 12U},
        {0x149bffdU, 13U},
        {0x14a5246U, 5U},
        {0x14a9bc4U, 7U},
        {0x14a9be7U, 7U},
        {0x14ad40eU, 9U},
        {0x14bc169U, 6U},
        {0x14bcc77U, 8U},
        {0x149cb38U, 20U},
        {0x1498fefU, 13U},
    }};
    for (size_t i = 0; i < spans.size(); ++i) {
        const auto [offset, length] = spans[i];
        if (offset > validated_rom.size() || length > validated_rom.size() - offset)
            throw std::runtime_error("Invalid ROM text span");
        const auto* first = reinterpret_cast<const char*>(validated_rom.data() + offset);
        result.text[i].assign(first, length);
        if (result.text[i].find('\0') != std::string::npos)
            throw std::runtime_error("Invalid ROM prompt text");
    }
    for (size_t i=0;i<result.weapons.size();++i) {
        const auto address = 0x800C469CU + uint32_t(i)*232;
        auto& value = result.weapons[i];
        value.damage = std::bit_cast<float>(source.word(address+0xC));
        value.magazine = source.byte(address+0x82);
        value.reload = source.byte(address+0x36);
        if (!std::isfinite(value.damage)) throw std::runtime_error("Invalid weapon metadata");
    }
    for (size_t i=0;i<result.ammo.size();++i) {
        const auto address = 0x800C7C14U + uint32_t(i)*4;
        result.ammo[i] = {source.half(address),source.half(address+2)};
    }
    return result;
}
void initialize(std::span<const uint8_t> validated_rom) {
    current = read_metadata(validated_rom);
    initialized = true;
}
const Metadata& metadata() {
    if (!initialized) throw std::logic_error("ROM metadata was used before validation");
    return current;
}
}
