#include "npc_protection.hpp"

#include <bit>
#include <cmath>

#include "modern_grapple.hpp"
#include "twine_qol.hpp"
#include "twine_recomp.h"

extern "C" uint32_t twine_npc_is_protected(uint8_t* rdram, uint32_t actor) {
    if (rdram == nullptr || twine::qol::settings().npc_protection !=
            twine::qol::NpcProtectionMode::On ||
            TWINE_MEM_W(0, 0x80102ED8U) != 0 ||
            (actor & 3U) != 0 ||
            !twine::grapple::rdram_range_valid(actor, 0x80U) ||
            TWINE_MEM_HU(0x78, actor) != 1U) {
        return 0;
    }
    const uint32_t state = TWINE_MEM_W(0x6C, actor);
    if ((state & 3U) != 0 ||
            !twine::grapple::rdram_range_valid(state, 0x8CU)) {
        return 0;
    }
    const float health = std::bit_cast<float>(
        static_cast<uint32_t>(TWINE_MEM_W(0x74, actor)));

    return std::isfinite(health) && health > 0.0f &&
        twine::npc_protection::friendly_role(TWINE_MEM_W(0x88, state));
}

extern "C" uint32_t twine_npc_filter_damage(uint8_t* rdram, uint32_t actor) {
    if (!twine_npc_is_protected(rdram, actor)) {
        return 0;
    }
    const uint32_t state = TWINE_MEM_W(0x6C, actor);
    const bool sleep = (TWINE_MEM_W(0x60, actor) & 0x100U) != 0 &&
        TWINE_MEM_W(0x64, state) == 34;
    TWINE_MEM_W(0x60, actor) &= ~twine::npc_protection::harm_events;
    TWINE_MEM_W(0x60, state) = 0;
    if (sleep) {
        TWINE_MEM_W(0x60, actor) |= 0x100U;
        return 2;
    }
    return 1;
}

extern "C" uint32_t twine_npc_projectile_policy(
    uint8_t* rdram, uint32_t actor, uint32_t projectile
) {
    if (!twine_npc_is_protected(rdram, actor)) { return 0; }
    if (!twine::grapple::rdram_range_valid(projectile, 0x30)) { return 1; }
    const uint32_t weapon = TWINE_MEM_W(0x2C, projectile);
    if (twine::grapple::rdram_range_valid(weapon, 0x68) &&
            (TWINE_MEM_HU(0, weapon) & 0x7FFFU) == 34 &&
            (TWINE_MEM_W(0x64, weapon) & 0x800U)) {

        return 2;
    }
    return 1;
}
