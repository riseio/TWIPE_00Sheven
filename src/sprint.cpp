#include "sprint.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>

#include "modern_grapple.hpp"
#include "twine_qol.hpp"
#include "twine_recomp.h"

namespace {
constexpr unsigned player_count = 4;
constexpr float speed_multiplier = 1.75f;

constexpr unsigned contact_grace_ticks = 2;
constexpr float contact_blend_step = (speed_multiplier - 1.0f) / 4.0f;
std::array<std::atomic<uint64_t>, player_count> requests{};
struct InteractionResult {
    uint32_t actor = 0;
    uint64_t epoch = 0;
    uint64_t tick = 0;
    bool consumed = false;
};

std::array<InteractionResult, player_count> interactions{};
struct ContactState {
    uint32_t actor = 0;
    uint32_t inventory = 0;
    uint32_t item = 0;
    uint64_t epoch = 0;
    uint64_t tick = 0;
    unsigned missing_ground = 0;
    float multiplier = 1.0f;
};
std::array<ContactState, player_count> contacts{};
uint64_t tick = 0;

float contact_multiplier(ContactState& state, uint32_t actor,
        uint32_t inventory, uint32_t item, uint64_t epoch, bool grounded) {
    if (state.actor != actor || state.inventory != inventory ||
            state.item != item || state.epoch != epoch ||
            (state.tick != tick && state.tick + 1 != tick)) {
        state = {actor, inventory, item, epoch, 0, 0,
            grounded ? speed_multiplier : 1.0f};
    }
    if (state.tick == tick) { return state.multiplier; }
    state.tick = tick;
    if (grounded) {
        state.missing_ground = 0;
        state.multiplier = std::fmin(speed_multiplier,
            state.multiplier + contact_blend_step);
    }
    else {

        if (state.missing_ground <= contact_grace_ticks) {
            ++state.missing_ground;
        }
        if (state.missing_ground > contact_grace_ticks) {
            state.multiplier = std::fmax(1.0f,
                state.multiplier - contact_blend_step);
        }
    }
    return state.multiplier;
}

bool valid(uint32_t address, uint32_t size) {
    return (address & 3U) == 0 &&
        twine::grapple::rdram_range_valid(address, size);
}

bool player_record(uint8_t* rdram, uint32_t actor, uint32_t& inventory,
        unsigned& player) {
    if (!rdram || !valid(actor, 0x80U) ||
            TWINE_MEM_HU(0x78, actor) != 2U ||
            TWINE_MEM_W(0, 0x80102ED8U) != 0) {
        return false;
    }
    inventory = TWINE_MEM_W(0x6C, actor);
    if (!valid(inventory, 0x184U)) { return false; }
    player = TWINE_MEM_BU(0x182, inventory);
    return player < player_count;
}

uint8_t action_flags(uint8_t* rdram, unsigned player, unsigned action) {
    return TWINE_MEM_BU(action * 2U, 0x80115094U + player * 0xA0U);
}

uint64_t request(unsigned player, uint64_t epoch) {
    const uint64_t value = requests[player].load(std::memory_order_acquire);
    return (value >> 3) == epoch ? value & 7U : 0;
}
}

namespace twine::sprint {
InputPublication::~InputPublication() {
    if (player_ >= 0 && static_cast<unsigned>(player_) < player_count) {
        requests[player_].store((qol::lifecycle_epoch() << 3) |
            (held ? 1U : 0U) | (forward ? 2U : 0U) |
            (dedicated ? 4U : 0U), std::memory_order_release);
    }
}

void begin_tick() { ++tick; }
}

extern "C" void twine_sprint_observe_interaction(uint8_t* rdram,
        recomp_context* ctx) {
    if (!ctx) { return; }

    const uint32_t actor = static_cast<uint32_t>(ctx->r19);
    uint32_t inventory = 0;
    unsigned player = player_count;
    if (!player_record(rdram, actor, inventory, player)) { return; }
    auto& result = interactions[player];
    const uint64_t epoch = twine::qol::lifecycle_epoch();

    const uint8_t flags = action_flags(rdram, player, 0x1D);
    if (twine::qol::settings().sprint != twine::qol::SprintMode::On ||
            (request(player, epoch) & 1U) == 0 || (flags & 4U) == 0) {
        result = {};
        return;
    }
    if (result.actor != actor || result.epoch != epoch ||
            (result.tick != tick &&
                (result.tick + 1 != tick || (flags & 0x10U) != 0))) {
        result = {actor, epoch, 0, false};
    }
    result.tick = tick;

    result.consumed = result.consumed || ctx->r22 == 0;
}

extern "C" void twine_sprint_apply(uint8_t* rdram, recomp_context* ctx) {
    if (!ctx) { return; }
    if (twine::qol::settings().sprint != twine::qol::SprintMode::On) {
        contacts = {};
        return;
    }
    const uint32_t actor = static_cast<uint32_t>(ctx->r18);
    uint32_t inventory = 0;
    unsigned player = player_count;
    if (!player_record(rdram, actor, inventory, player)) { return; }
    auto& contact = contacts[player];
    const uint64_t epoch = twine::qol::lifecycle_epoch();
    const auto& result = interactions[player];
    const uint8_t flags = action_flags(rdram, player, 0x1D);
    const uint64_t intent = request(player, epoch);

    const bool action_active = (intent & 1U) != 0 || (flags & 0x14U) != 0;
    const bool action_allows_sprint = !action_active ||
        ((intent & 1U) != 0 && (flags & 0x14U) == 4U &&
            result.actor == actor && result.epoch == epoch &&
            result.tick != 0 && result.tick + 1 == tick && !result.consumed);
    if ((intent & 5U) == 0 || (intent & 2U) == 0 || !action_allows_sprint ||
            (action_flags(rdram, player, 0x12) & 0x10U) != 0 ||
            TWINE_MEM_HU(0x7A, actor) != 1U ||
            TWINE_MEM_HU(0x7C, actor) != 0U ||
            TWINE_MEM_BU(0, 0x800CEEB0U) != 0 ||
            TWINE_MEM_H(0, 0x80117386U) <= 1 ||
            TWINE_MEM_W(0, 0x80102F2CU) != 0 ||
            twine::grapple::active(static_cast<int>(player))) {
        contact = {};
        return;
    }
    const uint32_t item = TWINE_MEM_W(0x68, actor);
    const uint32_t weapon_hud = TWINE_MEM_W(0xA8, inventory);
    const float health = std::bit_cast<float>(
        static_cast<uint32_t>(TWINE_MEM_W(0x74, actor)));
    if (!valid(item, 0x10U) || !valid(weapon_hud, 0x80U) ||
            !std::isfinite(health) || health <= 0.0f ||
            TWINE_MEM_H(0x7A, weapon_hud) != 0 ||
            (TWINE_MEM_HU(0x0C, item) & 0x245U) != 0U) {
        contact = {};
        return;
    }

    const float x = std::bit_cast<float>(static_cast<uint32_t>(ctx->r21));
    const float z = std::bit_cast<float>(static_cast<uint32_t>(ctx->r22));

    if (!std::isfinite(x) || !std::isfinite(z) ||
            std::abs(x) > 0.35f || std::abs(z) > 0.35f) {
        contact = {};
        return;
    }
    const float multiplier = contact_multiplier(contact, actor, inventory,
        item, epoch, (TWINE_MEM_HU(0x0C, item) & 8U) != 0);
    ctx->r21 = S32(std::bit_cast<uint32_t>(x * multiplier));
    ctx->r22 = S32(std::bit_cast<uint32_t>(z * multiplier));
}
