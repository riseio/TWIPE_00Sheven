#include "modern_input.hpp"
#include "twine_qol.hpp"
#include "twine_recomp.h"

namespace {
constexpr uint32_t item_count = 59;
constexpr uint32_t inventory_size = 0x410;
bool valid(uint32_t address, uint32_t size) {
    return (address & 3U) == 0 && address >= 0x80000000U &&
        address < 0x80800000U && size <= 0x80800000U - address;
}
bool enabled() {
    return twine::qol::settings().weapon_modes == twine::qol::WeaponModeMemory::On;
}
}

extern "C" void twine_restore_weapon_mode(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || !enabled()) return;
    const uint32_t actor = uint32_t(ctx->r4);
    if (!valid(actor, 0x70)) return;
    const uint32_t inventory = TWINE_MEM_W(0x6C, actor);
    const uint32_t item = TWINE_MEM_W(0x68, actor);
    if (!valid(inventory, inventory_size) || !valid(item, 0x14) ||
            TWINE_MEM_BU(0x182, inventory) >= 4) return;
    const uint8_t requested = TWINE_MEM_BU(0xF, item);
    if (requested >= item_count || requested == TWINE_MEM_BU(0xE, item)) return;
    const uint8_t family = twine::modern_input::weapon_family(requested);
    if (!TWINE_MEM_BU(0x1FC + family * 8, inventory)) return;

    const unsigned remembered = family + TWINE_MEM_BU(0x1FE + family * 8, inventory);
    if (remembered < item_count &&
            twine::modern_input::weapon_family(uint8_t(remembered)) == family) {
        TWINE_MEM_B(0xF, item) = remembered;
    }
}

extern "C" void twine_weapon_mode_fallback(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || enabled()) return;
    const uint32_t inventory = uint32_t(ctx->r20);
    const uint32_t family = uint32_t(ctx->r16);
    if (!valid(inventory, inventory_size) || family >= item_count ||
            twine::modern_input::weapon_family(uint8_t(family)) != family) return;

    TWINE_MEM_B(0x1FE + family * 8, inventory) = 0;
}
