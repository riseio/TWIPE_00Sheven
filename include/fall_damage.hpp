#ifndef FALL_DAMAGE_HPP
#define FALL_DAMAGE_HPP

#include <cstdint>
#include "cheats.hpp"

namespace twine::fall_damage {

constexpr bool protected_landing(uint8_t cheats_mask, bool grapple_owned) {
    return cheats::enabled(cheats_mask, cheats::Cheat::NoFallDamage) ||
        cheats::enabled(cheats_mask, cheats::Cheat::Invulnerability) ||
        grapple_owned;
}

}

#endif
