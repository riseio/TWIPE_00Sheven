#include "twine_qol.hpp"
#include "twine_recomp.h"

#include <bit>
#include <cmath>

namespace {
bool range(uint32_t address, uint32_t size) {
    return !(address & 3U) && address >= 0x80000000U &&
        uint64_t(address) + size <= 0x80800000ULL;
}
}

extern "C" uint32_t twine_keep_corpse(uint8_t* rdram, uint32_t actor) {
    if (!rdram || twine::qol::settings().corpses != twine::qol::KeepCorpses::On ||
            TWINE_MEM_W(0, 0x80102ED8U) != 0 || !range(actor, 0x84) ||
            TWINE_MEM_HU(0x78, actor) != 0x4C || TWINE_MEM_HU(0x7C, actor) != 12) {
        return 0;
    }
    const uint16_t action = TWINE_MEM_HU(0x7A, actor);
    const uint32_t state = TWINE_MEM_W(0x6C, actor);
    const float health = std::bit_cast<float>(uint32_t(TWINE_MEM_W(0x74, actor)));
    if ((action != 28 && action != 169) || !range(state, 0x8C) ||
            !std::isfinite(health) || health > 0 || (TWINE_MEM_W(0x60, actor) & 1U)) {
        return 0;
    }

    if (TWINE_MEM_BU(0x82, state) == 4) { return 0; }

    TWINE_MEM_B(0x82, actor) = 255;
    return 1;
}
