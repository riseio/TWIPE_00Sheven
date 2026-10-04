#include "modern_grapple.hpp"
#include "save_state_owner.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

#include "funcs.h"
#include "twine_qol.hpp"
#include "world_render_context.hpp"
#include "hud_crosshair.hpp"
#include "native_aim.hpp"
#include "twine_recomp.h"
#include "shared/rt64_modern_grapple.h"

namespace {

constexpr uint32_t actor_hit_address = 0x80108788;
constexpr float maximum_range = twine::grapple::maximum_range;
constexpr float maximum_pull_speed = 0.21f;
constexpr float verified_pull_distance = 0.05f;
constexpr uint16_t hook_flight_frames = 8;
constexpr uint16_t blocked_pull_frames = 30;
constexpr uint16_t fire_button = 0x2000;
constexpr uint16_t cancel_button = 0x4000;
constexpr size_t player_count = 4;

enum class Phase : uint8_t {
    Idle,
    Firing,
    Pulling,
    Landing,
    Released
};

struct State {
    uint64_t attachment_id = 0;
    twine::grapple::CollisionBounds collision{};
    std::array<std::array<twine::grapple::Vec3, 3>, 8> collision_contacts{};
    std::array<std::array<uint32_t, 4>, 8> contact_keys{};
    uint32_t contact_frame = 0;
    uint8_t contact_count = 0;
    bool contact_overflow = false;
    twine::grapple::Vec3 origin{};
    twine::grapple::Vec3 anchor{};
    twine::grapple::Vec3 anchor_normal{};
    twine::grapple::Vec3 destination{};
    twine::grapple::Vec3 hook{};
    twine::grapple::Vec3 pull_origin{};
    twine::grapple::Vec3 segment_origin{};
    std::array<twine::grapple::Vec3, 2> waypoints{};
    uint32_t owner = 0;
    uint64_t epoch = 0;
    float last_remaining = 0.0f;
    float maximum_path_error = 0.0f;
    float initial_yaw = 0.0f;
    float maximum_yaw_error = 0.0f;
    uint32_t frames = 0;
    uint16_t flight_frames = 0;
    uint16_t stalled_frames = 0;
    uint8_t waypoint_count = 0;
    uint8_t waypoint_index = 0;
    bool pull_verified = false;
    bool native_contact = false;
    bool submitted_pending = false;
    Phase phase = Phase::Idle;
} state;
uint64_t next_attachment_id = 0;
std::array<std::atomic<uint8_t>, player_count> equipped_items{};
std::array<std::atomic_bool, player_count> fire_held{};
std::array<std::atomic_bool, player_count> fire_requested{};
std::array<std::atomic_bool, player_count> cancel_requested{};
std::atomic_int active_player{-1};
std::atomic_bool aim_anchor_available{false};
std::atomic<uint32_t> fall_protected_player{0};
std::atomic<uint64_t> fall_protection_epoch{0};

using RayHit = twine::aim::Hit;
using twine::aim::cast_ray;

bool valid_anchor(const RayHit& hit, twine::grapple::Vec3 origin) {
    return twine::grapple::valid_grapple_anchor(
        hit.hit, hit.surface, hit.actor, hit.flags, origin, hit.point);
}

bool modern_enabled() {
    return twine::qol::settings().grapple ==
        twine::qol::GrappleMode::ModernPull;
}

float read_float(uint8_t* rdram, uint32_t base, uint32_t offset) {
    return std::bit_cast<float>(
        static_cast<uint32_t>(TWINE_MEM_W(offset, base)));
}

twine::grapple::Vec3 read_vec(uint8_t* rdram, uint32_t base, uint32_t offset) {
    return {
        read_float(rdram, base, offset),
        read_float(rdram, base, offset + 4),
        read_float(rdram, base, offset + 8),
    };
}

bool read_collision_bounds(uint8_t* rdram, uint32_t player,
        twine::grapple::CollisionBounds& bounds) {
    if (!twine::grapple::rdram_range_valid(player, 0x6CU)) { return false; }
    const uint32_t item = TWINE_MEM_W(0x68, player);
    if (!twine::grapple::rdram_range_valid(item, 0x3CU)) { return false; }
    const uint32_t model = TWINE_MEM_W(0, item);
    const uint32_t transformed = TWINE_MEM_W(4, item);
    if (!twine::grapple::rdram_range_valid(model, 0x44U) ||
            !twine::grapple::rdram_range_valid(transformed, 0xB0U)) { return false; }
    return twine::grapple::collision_bounds(read_float(rdram, player, 0x28),
        (read_float(rdram, model, 0x1C) + read_float(rdram, model, 0x40)) * 0.5f,
        read_float(rdram, transformed, 0xAC), read_float(rdram, item, 0x38), bounds);
}

bool read_aim(
    uint8_t* rdram,
    recomp_context* ctx,
    uint32_t player,
    twine::grapple::Vec3& origin,
    twine::grapple::Vec3& direction,
    uint32_t& item_state,
    uint32_t& room
) {
    if (!twine::grapple::rdram_range_valid(player, 0x6CU)) {
        return false;
    }
    item_state = TWINE_MEM_W(0x68, player);
    if (!twine::grapple::rdram_range_valid(item_state, 0x84U)) {
        return false;
    }
    twine::render::NativeCamera camera{};
    if (!twine::render::read_native_camera(rdram, ctx, player, camera)) { return false; }
    origin = {camera.position[0], camera.position[1], camera.position[2]};
    direction = {camera.aim[0], camera.aim[1], camera.aim[2]};
    room = camera.room;
    return twine::grapple::rdram_range_valid(room, 0x5CU);
}

twine::grapple::Vec3 current_pull_destination() {
    return state.waypoint_index < state.waypoint_count
        ? state.waypoints[state.waypoint_index]
        : state.destination;
}

bool advance_pull_segment(twine::grapple::Vec3 player) {
    if (state.waypoint_index >= state.waypoint_count) {
        return false;
    }
    state.segment_origin = player;
    ++state.waypoint_index;
    state.last_remaining = twine::grapple::length(
        current_pull_destination() - player);
    state.stalled_frames = 0;

    return true;
}

RayHit query_aim(
    uint8_t* rdram,
    recomp_context* ctx,
    uint32_t player,
    twine::grapple::Vec3& origin,
    twine::grapple::Vec3& direction,
    uint32_t& item_state
) {
    uint32_t room = 0;
    if (!read_aim(rdram, ctx, player, origin, direction, item_state, room)) {
        return {};
    }

    const RayHit hit = cast_ray(rdram, ctx, player, origin, direction,
        maximum_range, true, 0, room);
    return hit;
}

void cancel() {
    state = {};
    aim_anchor_available.store(false, std::memory_order_release);
    active_player.store(-1, std::memory_order_release);
    RT64::setModernGrappleLine({});
}

void clear_grapple_overlay() {
    RT64::setModernGrappleLine({});
}

void publish_state_line(
    twine::grapple::Vec3 camera,
    twine::grapple::Vec3 view
) {
    if (state.phase == Phase::Idle) {
        RT64::setModernGrappleLine({});
        return;
    }
    const auto end = state.phase == Phase::Firing
        ? state.hook : state.anchor;
    const auto start = state.phase == Phase::Firing
        ? state.origin
        : twine::grapple::pulling_cable_start(
            camera, view, state.anchor, state.anchor_normal);
    RT64::ModernGrappleLine line{};
    line.start = {start.x, start.y, start.z};
    line.end = {end.x, end.y, end.z};
    line.camera = {camera.x, camera.y, camera.z};
    line.forward = {view.x, view.y, view.z};
    line.visible = true;
    line.endpointLocked = state.phase != Phase::Firing;
    line.attachmentId = state.attachment_id;
    RT64::setModernGrappleLine(line);
}

}

namespace twine::grapple {

bool player_collision_bounds(uint8_t* rdram, uint32_t player, CollisionBounds& bounds) {
    return read_collision_bounds(rdram, player, bounds);
}

void invalidate_presentation() {
    RT64::setModernGrappleLine({});
}

void publish_world_active(bool active) {
    RT64::setModernWorldActive(active);
}

bool ready(int player) {
    return player >= 0 && static_cast<size_t>(player) < player_count &&
        modern_enabled() &&
        equipped_items[player].load(std::memory_order_acquire) == item_id &&
        active_player.load(std::memory_order_acquire) < 0;
}

bool active(int player) {
    return player >= 0 && static_cast<size_t>(player) < player_count &&
        active_player.load(std::memory_order_acquire) == player;
}

bool aim_anchor_ready(int player) {
    return ready(player) &&
        aim_anchor_available.load(std::memory_order_acquire);
}

bool consume_fall_damage_protection(uint32_t player_object) {
    if (fall_protection_epoch.load(std::memory_order_acquire) !=
            twine::qol::lifecycle_epoch()) {
        fall_protected_player.store(0, std::memory_order_release);
        return false;
    }
    return fall_protected_player.load(std::memory_order_acquire) ==
        player_object;
}

void handle_input(int player, uint16_t& buttons) {
    if (player < 0 || static_cast<size_t>(player) >= player_count) {
        return;
    }
    const bool down = (buttons & fire_button) != 0;
    const bool was_down = fire_held[player].exchange(
        down, std::memory_order_relaxed);
    const bool pressed = down && !was_down;
    const bool enabled = modern_enabled();
    const uint8_t equipped = equipped_items[player].load(
        std::memory_order_acquire);

    if (!enabled || equipped != item_id) {

        fire_held[player].store(false, std::memory_order_relaxed);
        fire_requested[player].store(false, std::memory_order_release);
        return;
    }
    if (active_player.load(std::memory_order_acquire) == player &&
            (buttons & cancel_button) != 0) {
        buttons &= static_cast<uint16_t>(~cancel_button);
        cancel_requested[player].store(true, std::memory_order_release);
    }

    buttons &= static_cast<uint16_t>(~fire_button);
    if (!down && active_player.load(std::memory_order_acquire) != player) {
        fire_requested[player].store(false, std::memory_order_release);
    }
    if (pressed && active_player.load(std::memory_order_acquire) != player) {
        fire_requested[player].store(true, std::memory_order_release);

    }
}

}

static bool begin_fire(
    uint8_t* rdram,
    uint32_t player,
    uint32_t player_index,
    RayHit hit,
    twine::grapple::Vec3 origin,
    twine::grapple::Vec3 direction
) {
    if (state.phase != Phase::Idle && state.owner == player) {

        return false;
    }
    cancel();
    if (!valid_anchor(hit, origin)) {

        return false;
    }

    state.origin = twine::grapple::watch_position(origin, direction);
    state.anchor = hit.point;
    state.hook = state.origin;
    const auto normal = twine::grapple::orient_surface_normal(
        hit.normal, origin, hit.point);
    if (twine::grapple::length(normal) < 0.99f) {

        return false;
    }
    state.anchor_normal = normal;
    if (!read_collision_bounds(rdram, player, state.collision)) {

        return false;
    }
    state.destination = twine::grapple::safe_surface_destination(
        hit.point, normal, state.collision);

    const float initial_distance = twine::grapple::length(hit.point - origin);
    const float traversal_distance = twine::grapple::length(
        state.destination - origin);
    const float pitch = read_float(rdram, player, 0x3C);
    const float yaw = read_float(rdram, player, 0x40);
    state.owner = player;
    state.attachment_id = ++next_attachment_id;
    state.initial_yaw = read_float(rdram, player, 0x40);
    state.epoch = twine::qol::lifecycle_epoch();
    state.phase = Phase::Firing;
    active_player.store(static_cast<int>(player_index), std::memory_order_release);

    return true;
}

extern "C" void twine_grapple_apply_pull(
    uint8_t* rdram, recomp_context* ctx
) {

    const uint32_t player_object = static_cast<uint32_t>(ctx->r18);
    uint32_t player_index = player_count;
    uint8_t equipped_item = 0;
    if (twine::grapple::rdram_range_valid(player_object, 0x6CU)) {
        const uint32_t inventory = TWINE_MEM_W(0x6C, player_object);
        const uint32_t item_state = TWINE_MEM_W(0x68, player_object);
        if (twine::grapple::rdram_range_valid(inventory, 0x183U)) {
            player_index = TWINE_MEM_BU(0x182, inventory);
        }
        if (twine::grapple::rdram_range_valid(item_state, 0x10U)) {
            equipped_item = TWINE_MEM_BU(0x0E, item_state);
        }
    }
    bool idle_anchor_valid = false;
    RayHit idle_candidate{};
    twine::grapple::Vec3 idle_origin{};
    twine::grapple::Vec3 idle_direction{};
    if (state.phase == Phase::Idle && player_index == 0 && modern_enabled() &&
            equipped_item == twine::grapple::item_id) {
        uint32_t item_state = 0;
        idle_candidate = query_aim(
            rdram,
            ctx,
            player_object,
            idle_origin,
            idle_direction,
            item_state);
        idle_anchor_valid = valid_anchor(idle_candidate, idle_origin);

    }
    twine::crosshair::update_colour(rdram, player_object,
        modern_enabled() && (idle_anchor_valid ||
            (state.owner == player_object &&
                (state.phase == Phase::Firing || state.phase == Phase::Pulling))),
        twine::crosshair::npc_under_aim(rdram, ctx, player_object));
    if (player_index < player_count) {

        equipped_items[player_index].store(
            equipped_item, std::memory_order_release);
        if (cancel_requested[player_index].exchange(
                false, std::memory_order_acq_rel) &&
                active_player.load(std::memory_order_acquire) ==
                    static_cast<int>(player_index)) {
            if (state.phase == Phase::Released) {
                return;
            }

            cancel();
            return;
        }
        const bool fire_allowed = modern_enabled() &&
            equipped_item == twine::grapple::item_id;
        if (!fire_allowed) {
            fire_requested[player_index].store(
                false, std::memory_order_release);
        }
        const bool fire_pending = fire_allowed && idle_anchor_valid &&
            fire_requested[player_index].exchange(
                false, std::memory_order_acq_rel);
        if (fire_pending) {
            begin_fire(
                rdram,
                player_object,
                player_index,
                idle_candidate,
                idle_origin,
                idle_direction);
        }
    }
    if (state.phase == Phase::Idle) {
        if (player_index == 0) {

            if (modern_enabled() &&
                    equipped_item == twine::grapple::item_id) {
                aim_anchor_available.store(
                    idle_anchor_valid,
                    std::memory_order_release);
                clear_grapple_overlay();
            }
            else {
                aim_anchor_available.store(false, std::memory_order_release);
                clear_grapple_overlay();
            }
        }
        return;
    }
    if (!modern_enabled() ||
            state.epoch != twine::qol::lifecycle_epoch() ||
            player_object != state.owner) {

        cancel();
        return;
    }
    if (player_index >= player_count ||
            !fire_held[player_index].load(std::memory_order_acquire)) {

        cancel();
        return;
    }

    if (!twine::grapple::rdram_range_valid(state.owner, 0x6CU)) {

        cancel();
        return;
    }
    auto player = read_vec(rdram, state.owner, 0x24);
    const auto current_view = twine::grapple::camera_direction(
        read_float(rdram, state.owner, 0x40),
        read_float(rdram, state.owner, 0x3C));
    state.maximum_yaw_error = std::max(
        state.maximum_yaw_error,
        std::abs(read_float(rdram, state.owner, 0x40) - state.initial_yaw));
    if (state.phase == Phase::Firing) {
        ctx->r21 = 0;
        ctx->r30 = 0;
        ctx->r22 = 0;

        ++state.frames;
        state.hook = twine::grapple::flight_position(
            state.origin, state.anchor, state.frames, hook_flight_frames);
        if (state.frames >= hook_flight_frames) {
            state.phase = Phase::Pulling;
            state.pull_origin = player;
            state.segment_origin = player;
            fall_protection_epoch.store(state.epoch, std::memory_order_release);
            fall_protected_player.store(state.owner, std::memory_order_release);
            state.last_remaining = twine::grapple::length(
                current_pull_destination() - player);
            state.maximum_path_error = 0.0f;
            state.flight_frames = state.frames;
            state.frames = 0;

        }
        return;
    }

    if (state.phase == Phase::Released) {
        if (fall_protected_player.load(std::memory_order_acquire) != state.owner) {
            cancel();
        }
        else {
            clear_grapple_overlay();
        }
        return;
    }

    if (state.phase == Phase::Landing) {
        fall_protection_epoch.store(state.epoch, std::memory_order_release);
        fall_protected_player.store(state.owner, std::memory_order_release);
        state.phase = Phase::Released;
        ctx->r21 = 0;
        ctx->r30 = 0;
        ctx->r22 = 0;
        clear_grapple_overlay();

        return;
    }

    fall_protection_epoch.store(state.epoch, std::memory_order_release);
    fall_protected_player.store(state.owner, std::memory_order_release);

    const auto pull_destination = current_pull_destination();
    const float moved = twine::grapple::length(player - state.pull_origin);
    const float remaining = twine::grapple::length(
        pull_destination - player);
    const float path_error = twine::grapple::distance_from_cable(
        state.segment_origin, player, pull_destination);
    ++state.frames;
    if (state.frames == 20) {
        const auto cable_start = twine::grapple::watch_position(
            player, current_view);

    }
    if (remaining < state.last_remaining - 0.001f) {
        state.maximum_path_error = std::max(
            state.maximum_path_error, path_error);
        state.last_remaining = remaining;
        state.stalled_frames = 0;
    }
    else {
        ++state.stalled_frames;
    }
    if (!state.pull_verified) {
        if (moved >= verified_pull_distance) {
            state.pull_verified = true;

        }
    }

    if (remaining <= twine::grapple::arrival_tolerance &&
            advance_pull_segment(player)) {
        ctx->r21 = 0;
        ctx->r30 = 0;
        ctx->r22 = 0;
        return;
    }

    const bool final_segment =
        state.waypoint_index >= state.waypoint_count;
    if (final_segment && state.pull_verified &&
            remaining <= twine::grapple::arrival_tolerance) {

        state.native_contact = false;
        state.phase = Phase::Landing;
        ctx->r21 = 0;
        ctx->r30 = 0;
        ctx->r22 = 0;
        clear_grapple_overlay();
        return;
    }
    const bool path_changed =
        path_error > twine::grapple::maximum_path_deviation;
    const bool contact_range = remaining <=
        twine::grapple::capsule_radius + twine::grapple::arrival_tolerance;
    if (final_segment && state.pull_verified && contact_range && !path_changed &&
            state.stalled_frames >= 3) {

        state.native_contact = true;
        state.phase = Phase::Landing;
        ctx->r21 = 0;
        ctx->r30 = 0;
        ctx->r22 = 0;
        clear_grapple_overlay();
        return;
    }
    if (final_segment && state.pull_verified && contact_range && path_changed) {

        if (path_error <= twine::grapple::capsule_radius) {

            state.native_contact = true;
            state.phase = Phase::Landing;
            ctx->r21 = 0;
            ctx->r30 = 0;
            ctx->r22 = 0;
            clear_grapple_overlay();
            return;
        }
        if (path_changed) {

            cancel();
            return;
        }
    }
    if (state.stalled_frames >= blocked_pull_frames) {

        cancel();
        return;
    }
    if (state.stalled_frames >= 3 && final_segment && !state.contact_overflow &&
            state.contact_count != 0 && state.contact_frame + 1 == state.frames) {
        if (twine::grapple::collision_clearance_route(
                player,
                state.destination,
                std::span(state.collision_contacts.data(), state.contact_count),
                state.collision,
                state.waypoints[0],
                state.waypoints[1])) {
            state.segment_origin = player;
            state.waypoint_count = 2;
            state.waypoint_index = 0;
            state.last_remaining = twine::grapple::length(
                state.waypoints[0] - player);
            state.stalled_frames = 0;

            ctx->r21 = 0;
            ctx->r30 = 0;
            ctx->r22 = 0;
            return;
        }
    }

    const auto step = twine::grapple::advance_along_segment(
        state.segment_origin,
        player,
        pull_destination,
        maximum_pull_speed);
    state.submitted_pending = true;
    ctx->r21 = std::bit_cast<uint32_t>(step.x);
    ctx->r30 = std::bit_cast<uint32_t>(step.y);
    ctx->r22 = std::bit_cast<uint32_t>(step.z);
}

extern "C" void twine_grapple_filter_gravity(
    uint8_t* rdram, recomp_context* ctx
) {
    (void)rdram;
    if (((state.phase == Phase::Pulling && state.submitted_pending) ||
            state.phase == Phase::Landing) &&
            static_cast<uint32_t>(ctx->r17) == state.owner) {
        ctx->f0.u32l = 0;
        ctx->f2.u32l = 0;
    }
}

extern "C" void twine_grapple_observe_surface(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t stack = uint32_t(ctx->r29);
    if (state.phase != Phase::Pulling ||
            !twine::grapple::rdram_range_valid(stack, 0x298) ||
            uint32_t(TWINE_MEM_W(0x194, stack)) != state.owner) { return; }
    const uint32_t owner = uint32_t(ctx->r22);
    if (!twine::grapple::rdram_range_valid(owner, 0x60)) { return; }
    if (state.contact_frame != state.frames) {
        state.contact_frame = state.frames;
        state.contact_count = 0;
        state.contact_overflow = false;
    }
    std::array<uint32_t, 4> key{owner, uint32_t(ctx->r19), uint32_t(ctx->r17), uint32_t(ctx->r18)};
    std::sort(key.begin() + 1, key.end());
    for (uint8_t i = 0; i < state.contact_count; ++i) {
        if (state.contact_keys[i] == key) { return; }
    }
    if (state.contact_count == state.collision_contacts.size()) {
        state.contact_overflow = true;
        return;
    }
    std::array<twine::grapple::Vec3, 3> triangle{};
    const uint32_t transform_flags = uint32_t(TWINE_MEM_W(0x5C, owner)) >> 23;
    for (unsigned i = 0; i < triangle.size(); ++i) {
        if (!twine::grapple::rdram_range_valid(key[i + 1], 12)) { return; }
        auto point = read_vec(rdram, key[i + 1], 0);
        if (transform_flags & 2U) {

            point = {
                twine::grapple::dot(read_vec(rdram, stack, 0x38), point),
                twine::grapple::dot(read_vec(rdram, stack, 0x44), point),
                twine::grapple::dot(read_vec(rdram, stack, 0x50), point)};
        }
        if (transform_flags & 1U) { point = point + read_vec(rdram, owner, 0x24); }
        triangle[i] = point;
    }
    state.contact_keys[state.contact_count] = key;
    state.collision_contacts[state.contact_count++] = triangle;

}

extern "C" void twine_grapple_finish_landing(
    uint8_t* rdram, recomp_context* ctx
) {
    const uint32_t player = static_cast<uint32_t>(ctx->r18);

    twine::render::publish_native_camera(rdram, ctx, player);

    if (player == state.owner &&
            (state.phase == Phase::Firing || state.phase == Phase::Pulling)) {
        twine::render::NativeCamera camera{};
        if (twine::render::read_native_camera(rdram, ctx, player, camera)) {
            publish_state_line(
                {camera.position[0], camera.position[1], camera.position[2]},
                {camera.forward[0], camera.forward[1], camera.forward[2]});
        }
        else {
            clear_grapple_overlay();
        }
    }
    if (state.phase == Phase::Pulling && player == state.owner &&
            state.frames != 0) {
        state.submitted_pending = false;
        return;
    }
    if (state.phase == Phase::Firing || state.phase == Phase::Pulling ||
            state.phase == Phase::Landing ||
            fall_protection_epoch.load(std::memory_order_acquire) !=
                twine::qol::lifecycle_epoch() ||
            fall_protected_player.load(std::memory_order_acquire) != player ||
            read_float(rdram, player, 0x74) <= 0.0f) {
        return;
    }

    if (!twine::grapple::rdram_range_valid(player, 0x6C)) { return; }
    const uint32_t item = TWINE_MEM_W(0x68, player);
    if (!twine::grapple::rdram_range_valid(item, 0xE) ||
            (TWINE_MEM_HU(0xC, item) & 8U) == 0) { return; }
    fall_protected_player.store(0, std::memory_order_release);

    if (player == state.owner && state.phase == Phase::Released) {
        cancel();
    }
}

namespace {
template<class A> void grapple_state_fields(A& a, State& s) {
    auto vec = [&](auto& v) {
        a.fields(v.x, v.y, v.z);
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            throw std::runtime_error("Nonfinite grapple state coordinate");
    };
    a.fields(s.attachment_id, s.contact_frame, s.contact_count, s.contact_overflow, s.owner, s.epoch, s.last_remaining, s.maximum_path_error, s.initial_yaw, s.maximum_yaw_error, s.frames, s.flight_frames, s.stalled_frames, s.waypoint_count, s.waypoint_index, s.pull_verified, s.native_contact, s.submitted_pending, s.phase);
    a.fields(s.collision.below, s.collision.above);
    vec(s.origin);
    vec(s.anchor);
    vec(s.anchor_normal);
    vec(s.destination);
    vec(s.hook);
    vec(s.pull_origin);
    vec(s.segment_origin);
    for (auto& v : s.waypoints) vec(v);
    for (auto& contact : s.collision_contacts) for (auto& v : contact) vec(v);
    for (auto& key : s.contact_keys) for (auto& v : key) a.fields(v);
}
}
twine::state::Bytes twine::state::capture_grapple(uint8_t*) {
    Writer out; out.u32(1);
    grapple_state_fields(out, ::state);
    out.fields(next_attachment_id, active_player.load(), aim_anchor_available.load(),
        fall_protected_player.load(), fall_protection_epoch.load(), uint64_t{0});
    for (auto& item : equipped_items) out.scalar(item.load());
    return std::move(out.bytes);
}
std::unique_ptr<twine::state::PreparedOwner> twine::state::prepare_grapple(std::span<const uint8_t> bytes) {
    Reader in(bytes); if (in.u32() != 1) throw std::runtime_error("Invalid grapple state schema");
    State saved; grapple_state_fields(in, saved);
    const auto next = in.u64(); const auto player = in.scalar<int>(); const auto aim = in.scalar<bool>();
    const auto protected_actor = in.u32(); const auto protection_epoch = in.u64();
    in.u64(); // Reserved counter in existing saves.
    std::array<uint8_t, 4> items; for (auto& item : items) item = in.scalar<uint8_t>();
    in.end();
    if (saved.phase > Phase::Released || saved.contact_count > saved.contact_keys.size() ||
        saved.waypoint_count > saved.waypoints.size() || saved.waypoint_index > saved.waypoint_count ||
        saved.attachment_id > next || player < -1 || player >= 4 ||
        (saved.owner && !guest_range(saved.owner, 0x84)) ||
        (protected_actor && !guest_range(protected_actor, 0x84)) ||
        !std::isfinite(saved.last_remaining) || !std::isfinite(saved.maximum_path_error) ||
        !std::isfinite(saved.initial_yaw) || !std::isfinite(saved.maximum_yaw_error) ||
        !std::isfinite(saved.collision.above) || !std::isfinite(saved.collision.below))
        throw std::runtime_error("Invalid grapple state");
    return prepared_owner([saved, next, player, aim, protected_actor, protection_epoch, items]() mutable noexcept {
        const auto epoch = qol::lifecycle_epoch();
        const bool protected_now = saved.epoch == protection_epoch;
        saved.epoch = epoch; ::state = saved; next_attachment_id = next;
        active_player.store(player); aim_anchor_available.store(aim);
        fall_protected_player.store(protected_actor);
        fall_protection_epoch.store(protected_now ? epoch : 0);
        for (size_t i = 0; i < items.size(); ++i) {
            equipped_items[i].store(items[i]); fire_held[i].store(false);
            fire_requested[i].store(false); cancel_requested[i].store(false);
        }

    });
}
