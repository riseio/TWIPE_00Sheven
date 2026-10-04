#include "fall_damage.hpp"

#include "modern_grapple.hpp"
#include "twine_recomp.h"

extern "C" uint32_t twine_should_skip_fall_damage(
    uint8_t* rdram, recomp_context* ctx
) {

    if (rdram == nullptr || ctx == nullptr ||
            TWINE_MEM_W(0, 0x80102ED8U) != 0) {
        return 0;
    }
    const uint32_t player = static_cast<uint32_t>(ctx->r17);
    if (!twine::grapple::rdram_range_valid(player, 0x7CU) ||
            (player & 3U) != 0 ||
            player != static_cast<uint32_t>(TWINE_MEM_W(0, 0x8010A500U)) ||
            static_cast<uint32_t>(ctx->r18) !=
                static_cast<uint32_t>(TWINE_MEM_W(0x6C, player))) {
        return 0;
    }
    return twine::fall_damage::protected_landing(
        twine::cheats::selected_mask(),
        twine::grapple::consume_fall_damage_protection(player)) ? 1U : 0U;
}
