#include "native_aim.hpp"
#include "hud_crosshair.hpp"
#include "npc_protection.hpp"
#include "world_render_context.hpp"
#include "funcs.h"
#include <bit>

namespace {
constexpr uint32_t static_hit_address = 0x80108F84;
constexpr uint32_t actor_hit_address = 0x80108788;
float read_float(uint8_t* rdram, uint32_t base, uint32_t offset) {
    return std::bit_cast<float>(
        static_cast<uint32_t>(TWINE_MEM_W(offset, base)));
}

void write_float(uint8_t* rdram, uint32_t base, uint32_t offset, float value) {
    TWINE_MEM_W(offset, base) = std::bit_cast<uint32_t>(value);
}

twine::grapple::Vec3 read_vec(uint8_t* rdram, uint32_t base, uint32_t offset) {
    return {
        read_float(rdram, base, offset),
        read_float(rdram, base, offset + 4),
        read_float(rdram, base, offset + 8),
    };
}

}
namespace twine::aim {
Hit cast_ray(
    uint8_t* rdram,
    recomp_context* ctx,
    uint32_t player,
    twine::grapple::Vec3 origin,
    twine::grapple::Vec3 direction,
    float distance,
    bool include_actors,
    uint32_t forced_actor,
    uint32_t initial_room,
    uint32_t npc_mask
) {
    Hit hit{};
    const recomp_context saved = *ctx;
    const uint32_t call_sp = static_cast<uint32_t>(saved.r29) - 0x80U;
    if (!std::isfinite(distance) || distance <= 0.0f ||
            !twine::grapple::rdram_range_valid(call_sp - 0x2B0U, 0x330U) ||
            !twine::grapple::rdram_range_valid(player, 0x24U)) {
        return hit;
    }

    const uint32_t origin_address = call_sp + 0x28U;
    const uint32_t endpoint_address = call_sp + 0x38U;
    const uint32_t output_address = call_sp + 0x48U;
    const uint32_t plane_address = call_sp + 0x58U;
    const uint32_t kind_address = call_sp + 0x68U;
    const uint32_t room = initial_room != 0 ? initial_room : TWINE_MEM_W(0x20, player);
    if (!twine::grapple::rdram_range_valid(room, 0x5CU)) { return hit; }
    const auto endpoint = origin + direction * distance;
    if (!std::isfinite(endpoint.x) || !std::isfinite(endpoint.y) ||
            !std::isfinite(endpoint.z)) { return hit; }
    for (uint32_t offset = 0; offset < 12; offset += 4) {
        write_float(rdram, output_address, offset, 0.0f);
    }
    for (uint32_t offset = 0; offset < 16; offset += 4) {
        write_float(rdram, plane_address, offset, 0.0f);
    }
    write_float(rdram, origin_address, 0, origin.x);
    write_float(rdram, origin_address, 4, origin.y);
    write_float(rdram, origin_address, 8, origin.z);
    write_float(rdram, endpoint_address, 0, endpoint.x);
    write_float(rdram, endpoint_address, 4, endpoint.y);
    write_float(rdram, endpoint_address, 8, endpoint.z);
    TWINE_MEM_W(0, kind_address) = 0;

    const uint32_t saved_static_hit = TWINE_MEM_W(0, static_hit_address);
    const uint32_t saved_actor_hit = TWINE_MEM_W(0, actor_hit_address);
    ctx->r29 = static_cast<gpr>(static_cast<int32_t>(call_sp));
    ctx->r4 = static_cast<gpr>(static_cast<int32_t>(origin_address));
    ctx->r5 = static_cast<gpr>(static_cast<int32_t>(endpoint_address));
    ctx->r6 = static_cast<gpr>(static_cast<int32_t>(room));
    ctx->r7 = static_cast<gpr>(static_cast<int32_t>(output_address));
    TWINE_MEM_W(0x10, call_sp) = kind_address;
    TWINE_MEM_W(0x14, call_sp) = plane_address;
    TWINE_MEM_W(0x18, call_sp) = npc_mask;
    TWINE_MEM_W(0x1C, call_sp) = include_actors ? 1 : 0;

    TWINE_MEM_W(0x20, call_sp) = forced_actor;

    func_800158A4(rdram, ctx);

    hit.collision_kind = ctx->r2 == 0 ? TWINE_MEM_W(0, kind_address) : 0;
    hit.hit = ctx->r2 == 0 && hit.collision_kind == 1;
    hit.surface = TWINE_MEM_W(0, static_hit_address);
    hit.actor = TWINE_MEM_W(0, actor_hit_address);
    hit.point = read_vec(rdram, output_address, 0);
    hit.normal = twine::grapple::normalized(read_vec(rdram, plane_address, 0));
    hit.plane_distance = read_float(rdram, plane_address, 12);
    if (hit.collision_kind == 1 && twine::grapple::rdram_range_valid(hit.surface, 0x54U)) {
        hit.flags = TWINE_MEM_W(0x50, hit.surface);
        hit.kind = TWINE_MEM_BU(0x48, hit.surface);
    }
    else if (twine::grapple::rdram_range_valid(hit.actor, 0x60U)) {
        hit.flags = TWINE_MEM_W(0x5C, hit.actor);
    }

    TWINE_MEM_W(0, static_hit_address) = saved_static_hit;
    TWINE_MEM_W(0, actor_hit_address) = saved_actor_hit;
    *ctx = saved;
    return hit;
}

}

namespace twine::crosshair {
Target npc_under_aim(uint8_t* rdram, recomp_context* ctx, uint32_t player) {
    render::NativeCamera camera{};
    if (!ctx || !render::read_native_camera(rdram, ctx, player, camera)) { return Target::None; }
    const uint32_t inventory = TWINE_MEM_W(0x6C, player);
    if (!grapple::rdram_range_valid(inventory, 4)) { return Target::None; }

    const auto hit = aim::cast_ray(rdram, ctx, player,
        {camera.position[0], camera.position[1], camera.position[2]},
        {camera.aim[0], camera.aim[1], camera.aim[2]},
        grapple::maximum_range, true, 0, camera.room, TWINE_MEM_W(0, inventory));
    if (hit.collision_kind != 2 || !grapple::rdram_range_valid(hit.actor, 0x80) ||
            TWINE_MEM_HU(0x78, hit.actor) != 1) { return Target::None; }
    const float health = std::bit_cast<float>(uint32_t(TWINE_MEM_W(0x74, hit.actor)));
    const uint32_t state = TWINE_MEM_W(0x6C, hit.actor);
    if (!std::isfinite(health) || health <= 0 || (state & 3U) != 0 ||
            !grapple::rdram_range_valid(state, 0x8C)) { return Target::None; }
    const uint32_t flags = TWINE_MEM_W(0x88, state);
    if (npc_protection::friendly_role(flags)) { return Target::Friendly; }
    return ((flags >> 21U) & 0xFU) == 1U ? Target::Enemy : Target::None;
}

}
