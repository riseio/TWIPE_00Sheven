#ifndef CIVILIAN_HEALTH_HPP
#define CIVILIAN_HEALTH_HPP

#include <cstdint>

namespace twine::civilian_health {

inline constexpr uint32_t role_mask = 0x01E00000U;
inline constexpr uint32_t noncombatant_role = 0x00600000U;

constexpr bool eligible(uint32_t role_flags, uint8_t current_item) {
    return (role_flags & role_mask) == noncombatant_role && current_item == 0;
}

constexpr float initial_health(
    float stock_health,
    bool double_civilian_health,
    uint32_t role_flags,
    uint8_t current_item
) {
    return double_civilian_health && eligible(role_flags, current_item)
        ? stock_health * 2.0f
        : stock_health;
}

}

#endif
