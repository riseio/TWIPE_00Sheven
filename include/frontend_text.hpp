#pragma once

#include <cstdint>

namespace twine::frontend {

constexpr bool hidden_text(uint32_t resource) {
    switch (resource) {
    case 0x0001:
    case 0x0181: // Short copyright footer.
    case 0x01B0:
    case 0x0238:
    case 0x02AA:
    case 0x00E9:
    case 0x00EA:
    case 0x00EB:
    case 0x00EC:
    case 0x00ED:
        return true;
    default:
        return false;
    }
}

uint32_t visible_text(uint8_t* rdram, uint32_t address, uint32_t resource);
}
