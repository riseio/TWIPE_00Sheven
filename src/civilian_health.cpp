#include "civilian_health.hpp"

#include <bit>
#include <cstdint>

#include "twine_qol.hpp"
#include "twine_recomp.h"

extern "C" void twine_apply_civilian_health(
    uint8_t* rdram,
    recomp_context* ctx
) {
    const uint32_t actor = static_cast<uint32_t>(ctx->r2);
    if (actor == 0 || twine::qol::settings().civilian_health !=
            twine::qol::CivilianHealthMode::Double) {
        return;
    }

    const uint32_t item_state = TWINE_MEM_W(0x68, actor);
    const uint32_t actor_state = TWINE_MEM_W(0x6C, actor);
    if (item_state == 0 || actor_state == 0) {
        return;
    }

    const float stock_health = std::bit_cast<float>(
        static_cast<uint32_t>(TWINE_MEM_W(0x74, actor)));
    const float health = twine::civilian_health::initial_health(
        stock_health,
        true,
        TWINE_MEM_W(0x88, actor_state),
        TWINE_MEM_BU(0x0E, item_state));
    TWINE_MEM_W(0x74, actor) = std::bit_cast<uint32_t>(health);
}
