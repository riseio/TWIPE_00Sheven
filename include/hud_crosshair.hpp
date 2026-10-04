#pragma once
#include <cstdint>
#include "twine_recomp.h"

namespace twine::crosshair {
enum class Target : uint8_t { None, Enemy, Friendly };

void update_colour(uint8_t* rdram, uint32_t player, bool grapple_valid, Target target = Target::None);
Target npc_under_aim(uint8_t* rdram, recomp_context* ctx, uint32_t player);
}
