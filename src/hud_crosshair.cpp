#include "hud_crosshair.hpp"
#include "twine_recomp.h"
#include "modern_grapple.hpp"

namespace twine::crosshair {
void update_colour(uint8_t* rdram, uint32_t player, bool grapple_valid, Target target) {
    if (!rdram || !grapple::rdram_range_valid(player, 0x70)) { return; }
    const uint32_t inventory = TWINE_MEM_W(0x6C, player);
    const uint32_t item = TWINE_MEM_W(0x68, player);
    if (!grapple::rdram_range_valid(inventory, 0x414) ||
            !grapple::rdram_range_valid(item, 0x10)) { return; }
    const uint32_t node = TWINE_MEM_W(0x410, inventory);
    if (!grapple::rdram_range_valid(node, 0x14)) { return; }

    if (TWINE_MEM_BU(0xE, item) == grapple::item_id) {
        if ((TWINE_MEM_HU(0xC, item) & 1U) == 0) {
            TWINE_MEM_W(0x10, node) = grapple_valid ? 0x3FFF3FFFU : 0xFF0000FFU;
        }
        return;
    }
    TWINE_MEM_W(0x10, node) = target == Target::Enemy ? 0xFF0000FFU :
        target == Target::Friendly ? 0x3FFF3FFFU : 0xFFFFFFFFU;
}
}
