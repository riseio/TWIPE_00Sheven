#ifndef TWINE_NPC_PROTECTION_HPP
#define TWINE_NPC_PROTECTION_HPP

#include <cstdint>

namespace twine::npc_protection {

constexpr bool friendly_role(uint32_t flags) {
    const uint32_t role = (flags >> 21U) & 0xFU;
    return role == 2U || role == 3U;
}

inline constexpr uint32_t harm_events = 0x440001E2U;

}

#endif
