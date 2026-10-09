#include "modern_grapple.hpp"
#include "twine_qol.hpp"
#include "twine_recomp.h"
#include "funcs.h"
#include <bit>
#include <cmath>

namespace {
unsigned remaining_world_puffs = 0;
bool valid(uint32_t address, uint32_t size) {
    return (address & 3U) == 0 && twine::grapple::rdram_range_valid(address, size);
}
bool enhanced() {
    return twine::qol::settings().impacts == twine::qol::ImpactEffects::Enhanced;
}
bool puff_resident(uint8_t* rdram, unsigned model) {
    constexpr uint32_t bank = 0x800C0784U + 251 * 16;
    const auto models = uint32_t(TWINE_MEM_W(4, bank));
    const auto count = uint32_t(TWINE_MEM_W(8, bank));
    return TWINE_MEM_W(0, bank) != 0 && count > model && count <= 4096 &&
        valid(models, count * 76) && TWINE_MEM_BU(model * 76 + 0x45, models) != 0;
}
void put_float(uint8_t* rdram, uint32_t address, float value) {
    TWINE_MEM_W(0, address) = std::bit_cast<uint32_t>(value);
}
}

extern "C" void twine_begin_impact_tick() {

    remaining_world_puffs = 8;
}

extern "C" void twine_character_impact(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || !enhanced() || !puff_resident(rdram, 27)) return;
    const auto actor = uint32_t(ctx->r23), projectile = uint32_t(ctx->r18);
    const auto sp = uint32_t(ctx->r29);
    if (!valid(actor, 0x7C) || !valid(projectile, 0x30) || !valid(sp, 0x2C)) return;
    const auto kind = TWINE_MEM_HU(0x78, actor);
    if (kind != 1 && kind != 2) return;
    const auto weapon = uint32_t(TWINE_MEM_W(0x2C, projectile));
    if (!valid(weapon, 0x68) || (TWINE_MEM_W(0x64, weapon) & 0x800)) return;

    put_float(rdram, sp + 0x10, 0.20f);
    put_float(rdram, sp + 0x14, 1.035f);
    TWINE_MEM_W(0x18, sp) = 27;
    TWINE_MEM_W(0x1C, sp) = 251;
    TWINE_MEM_W(0x20, sp) = 0x22;
    TWINE_MEM_W(0x24, sp) = 0xB01818C0U;
    TWINE_MEM_W(0x28, sp) = 0x80808018U;
}

extern "C" void twine_world_impact(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || !enhanced() || remaining_world_puffs == 0) return;
    const auto sp = uint32_t(ctx->r29), projectile = uint32_t(ctx->r19);
    const auto call_sp = sp - 0x60U;
    if (!valid(sp, 0x190) || !valid(call_sp - 0x400U, 0x460) ||
            !valid(projectile, 0x24)) return;

    if (TWINE_MEM_W(0x18C, sp) == 5) return;
    const auto room = uint32_t(TWINE_MEM_W(0x20, projectile));
    if (!valid(room, 0x5C)) return;

    if (!puff_resident(rdram, 25)) return;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto point = std::bit_cast<float>(uint32_t(TWINE_MEM_W(0x30 + axis * 4, sp)));
        const auto normal = std::bit_cast<float>(uint32_t(TWINE_MEM_W(0x70 + axis * 4, sp)));
        if (!std::isfinite(point) || !std::isfinite(normal) || std::abs(normal) > 1.01f) return;
        put_float(rdram, call_sp + 0x30 + axis * 4, point);
        put_float(rdram, call_sp + 0x40 + axis * 4, normal * 0.012f);
    }
    put_float(rdram, call_sp + 0x10, 0.20f);
    put_float(rdram, call_sp + 0x14, 1.035f);
    TWINE_MEM_W(0x18, call_sp) = 25;
    TWINE_MEM_W(0x1C, call_sp) = 251;
    TWINE_MEM_W(0x20, call_sp) = 0x22;
    TWINE_MEM_W(0x24, call_sp) = 0xC0B8A8A0U;
    TWINE_MEM_W(0x28, call_sp) = 0x80808010U;
    auto call = *ctx;
    call.r29 = twine_n64_address(call_sp);
    call.r4 = twine_n64_address(room);
    call.r5 = twine_n64_address(call_sp + 0x30);
    call.r6 = 0;
    call.r7 = twine_n64_address(call_sp + 0x40);
    --remaining_world_puffs;

    func_800617D8(rdram, &call);
}
