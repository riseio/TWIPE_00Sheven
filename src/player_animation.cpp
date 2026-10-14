#include "twine_recomp.h"

#include <bit>
#include <cmath>

namespace {
bool range(uint32_t address, uint32_t size) {
    return !(address & 3U) && address >= 0x80000000U &&
        uint64_t(address) + size <= 0x80800000ULL;
}
}

extern "C" void twine_crouch_blend_height(uint8_t* rdram, uint32_t actor) {
    if (!rdram || !range(actor, 0x80) || TWINE_MEM_HU(0x78, actor) != 2 ||
            TWINE_MEM_HU(0x7A, actor) == 0 || TWINE_MEM_HU(0x7C, actor) != 6) {
        return;
    }
    const uint32_t model = TWINE_MEM_W(0x68, actor);
    if (!range(model, 0xA4)) { return; }
    switch (TWINE_MEM_HU(0xA2, model)) {
    case 0x17D:
    case 0x183:
    case 0x189:
    case 0x18F:
        break;
    default:
        return;
    }
    const uint32_t previous = TWINE_MEM_W(0x54, model);
    const uint32_t current = TWINE_MEM_W(0x5C, model);
    if (!range(previous, 0x10) || !range(current, 0x10)) { return; }
    const uint32_t height = TWINE_MEM_W(0xC, current);
    if (!std::isfinite(std::bit_cast<float>(height))) { return; }

    TWINE_MEM_W(0xC, previous) = height;
}
