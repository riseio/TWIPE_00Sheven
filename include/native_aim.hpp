#pragma once
#include "modern_grapple.hpp"
#include "twine_recomp.h"

namespace twine::aim {
struct Hit {
    twine::grapple::Vec3 point{};
    twine::grapple::Vec3 normal{};
    float plane_distance = 0.0f;
    uint32_t surface = 0;
    uint32_t actor = 0;
    uint32_t flags = 0;
    uint8_t kind = 0;
    bool hit = false;
    uint32_t collision_kind = 0;
};

Hit cast_ray(uint8_t* rdram, recomp_context* ctx, uint32_t player,
    grapple::Vec3 origin, grapple::Vec3 direction, float distance,
    bool include_actors, uint32_t forced_actor, uint32_t initial_room = 0,
    uint32_t npc_mask = 0);
}
