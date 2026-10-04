#include "health_regeneration.hpp"
#include "save_state_owner.hpp"
#include "twine_qol.hpp"
#include "twine_recomp.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace twine::health {
namespace {
Recovery recovery;
bool range(uint32_t address, uint32_t size) {
    return !(address & 3U) && address >= 0x80000000U &&
        uint64_t(address) + size <= 0x80800000ULL;
}
float value(uint8_t* rdram, uint32_t address) {
    return std::bit_cast<float>(uint32_t(TWINE_MEM_W(0, address)));
}
bool player(uint8_t* rdram, uint32_t& actor, uint32_t& inventory) {
    if (!rdram || qol::settings().health_regeneration != qol::HealthRegenerationMode::On ||
            TWINE_MEM_W(0, 0x80102ED8U) != 0) { return false; }
    actor = TWINE_MEM_W(0, 0x8010A500U);
    if (!range(actor, 0x84) || TWINE_MEM_HU(0x78, actor) != 2) { return false; }
    inventory = TWINE_MEM_W(0x6C, actor);
    return range(inventory, 0x184) && TWINE_MEM_BU(0x182, inventory) == 0;
}
}

void begin_tick(uint8_t* rdram) {
    uint32_t actor = 0, inventory = 0;
    if (!player(rdram, actor, inventory)) { recovery = {}; return; }
    const auto epoch = qol::lifecycle_epoch();
    if (recovery.actor != actor || recovery.inventory != inventory || recovery.epoch != epoch) {
        recovery = {.actor = actor, .inventory = inventory, .epoch = epoch,
            .health = value(rdram, actor + 0x74), .armour = value(rdram, inventory + 0x60)};
    }

    recovery.damaged = recovery.damaged || value(rdram, actor + 0x74) < recovery.health ||
        value(rdram, inventory + 0x60) < recovery.armour;
}

void damage(uint8_t*, uint32_t actor) {
    if (recovery.actor == actor) { recovery.damaged = true; }
}

void finish_tick(uint8_t* rdram, bool gameplay) {
    uint32_t actor = 0, inventory = 0;
    if (!gameplay || !player(rdram, actor, inventory) || recovery.actor != actor ||
            recovery.inventory != inventory || recovery.epoch != qol::lifecycle_epoch()) {
        recovery = {};
        return;
    }
    float health = value(rdram, actor + 0x74);
    const float armour = value(rdram, inventory + 0x60);

    const float maximum = value(rdram, inventory + 0x68);
    if (!std::isfinite(health) || !std::isfinite(armour) || !std::isfinite(maximum) ||
            health <= 0 || maximum <= 0 || TWINE_MEM_HU(0x7A, actor) == 0) {
        recovery = {};
        return;
    }
    if (recovery.damaged || health < recovery.health || armour < recovery.armour) {
        recovery.quiet_ticks = 0;
    } else if (recovery.quiet_ticks < 150) {
        ++recovery.quiet_ticks;
    } else if (health < maximum) {
        health = std::min(maximum, health + maximum / 300.0f);
        TWINE_MEM_W(0x74, actor) = std::bit_cast<uint32_t>(health);
    }
    recovery.health = health;
    recovery.armour = armour;
    recovery.damaged = false;
}
}

extern "C" void twine_regeneration_damage(uint8_t* rdram, recomp_context* ctx) {
    twine::health::damage(rdram, uint32_t(ctx->r18));
}

twine::state::Bytes twine::state::capture_health(uint8_t*) {
    const auto& r = health::recovery;
    Writer out; out.u32(1);
    out.fields(r.actor, r.inventory, r.epoch, r.health, r.armour, r.quiet_ticks, r.damaged);
    return std::move(out.bytes);
}
std::unique_ptr<twine::state::PreparedOwner> twine::state::prepare_health(std::span<const uint8_t> bytes) {
    Reader in(bytes); if (in.u32() != 1) throw std::runtime_error("Invalid health state schema");
    health::Recovery r;
    in.fields(r.actor, r.inventory, r.epoch, r.health, r.armour, r.quiet_ticks, r.damaged); in.end();
    if ((r.actor && !guest_range(r.actor, 0x84)) || (r.inventory && !guest_range(r.inventory, 0x184)) ||
        !std::isfinite(r.health) || !std::isfinite(r.armour) || r.quiet_ticks > 150)
        throw std::runtime_error("Invalid health recovery state");
    return prepared_owner([r]() mutable noexcept { r.epoch = qol::lifecycle_epoch(); health::recovery = r; });
}
