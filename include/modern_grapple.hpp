#ifndef MODERN_GRAPPLE_HPP
#define MODERN_GRAPPLE_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace twine::grapple {

inline constexpr uint32_t item_id = 36;
inline constexpr float capsule_radius = 0.55f;
inline constexpr float arrival_tolerance = 0.10f;

inline constexpr float maximum_range = 32.0f;
inline constexpr uint32_t body_collision_flag = 0x200U;
inline constexpr float maximum_path_deviation = 0.10f;
inline constexpr float angle_units_per_turn = 4096.0f;
inline constexpr float tau = 6.2831853071795864769f;

struct Vec3 {
    float x;
    float y;
    float z;
};

struct CollisionBounds {
    float below;
    float above;
};

bool player_collision_bounds(uint8_t* rdram, uint32_t player, CollisionBounds& bounds);

inline bool collision_bounds(float player_y, float model_bottom,
        float model_top, float floor_offset, CollisionBounds& bounds) {

    if (!std::isfinite(player_y) || !std::isfinite(model_bottom) ||
            !std::isfinite(model_top) || !std::isfinite(floor_offset) ||
            model_bottom > player_y || model_top < player_y ||
            floor_offset <= 0.0f) { return false; }
    bounds = {std::max(player_y - model_bottom, floor_offset),
        std::max(model_top - player_y, 1.0f)};
    return std::isfinite(bounds.below) && std::isfinite(bounds.above);
}

inline Vec3 operator+(Vec3 left, Vec3 right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

inline Vec3 operator-(Vec3 left, Vec3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

inline Vec3 operator*(Vec3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

inline float length(Vec3 value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

inline Vec3 normalized(Vec3 value) {
    const float magnitude = length(value);
    if (!std::isfinite(magnitude) || magnitude <= 0.0001f) {
        return {};
    }
    return value * (1.0f / magnitude);
}

inline Vec3 camera_direction(float yaw, float pitch) {
    const float yaw_radians = yaw * tau / angle_units_per_turn;
    const float pitch_radians = pitch * tau / angle_units_per_turn;
    const float horizontal = std::cos(pitch_radians);
    return normalized({
        std::sin(yaw_radians) * horizontal,
        -std::sin(pitch_radians),
        std::cos(yaw_radians) * horizontal,
    });
}

inline bool valid_grapple_anchor(
    bool hit,
    uint32_t surface,
    uint32_t actor,
    uint32_t collision_flags,
    Vec3 origin,
    Vec3 point
) {
    const float distance = length(point - origin);

    return hit && (surface != 0 || actor != 0) &&
        (collision_flags & body_collision_flag) != 0 &&
        std::isfinite(distance) && distance > arrival_tolerance &&
        distance <= maximum_range;
}

inline float dot(Vec3 left, Vec3 right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline Vec3 cross(Vec3 left, Vec3 right) {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

inline Vec3 watch_position(Vec3 player, Vec3 view) {
    view = normalized(view);
    auto right = normalized(cross(view, {0.0f, 1.0f, 0.0f}));
    if (length(right) < 0.99f) {
        right = {1.0f, 0.0f, 0.0f};
    }
    const auto up = normalized(cross(right, view));
    return player + right * -0.36f + up * -0.77f + view * 2.0f;
}

inline Vec3 pulling_cable_start(
    Vec3 player,
    Vec3 view,
    Vec3 anchor,
    Vec3 anchor_normal
) {
    const Vec3 watch_offset = watch_position(player, view) - player;
    const float watch_offset_length = length(watch_offset);
    const Vec3 anchor_delta = anchor - player;
    const float anchor_distance = length(anchor_delta);
    if (!std::isfinite(watch_offset_length) ||
            watch_offset_length <= 0.0001f ||
            !std::isfinite(anchor_distance) || anchor_distance <= 0.0001f) {
        return player;
    }
    const Vec3 anchor_direction = anchor_delta * (1.0f / anchor_distance);
    const float authored_advance = dot(watch_offset, anchor_direction);
    const float maximum_advance = anchor_distance * 0.5f;
    Vec3 retained_offset = watch_offset;
    if (std::isfinite(authored_advance) && authored_advance > maximum_advance) {

        retained_offset = retained_offset + anchor_direction *
            (maximum_advance - authored_advance);
    }

    Vec3 start = player + retained_offset;
    anchor_normal = normalized(anchor_normal);
    const float player_clearance = dot(player - anchor, anchor_normal);
    const float start_clearance = dot(start - anchor, anchor_normal);
    if (length(anchor_normal) >= 0.99f &&
            std::isfinite(player_clearance) &&
            std::isfinite(start_clearance) && player_clearance > 0.0f) {

        const float minimum_clearance = player_clearance * 0.5f;
        if (start_clearance < minimum_clearance) {
            start = start + anchor_normal *
                (minimum_clearance - start_clearance);
        }
    }
    return start;
}

inline float capsule_support_distance(Vec3 normal, CollisionBounds bounds) {
    normal = normalized(normal);
    return capsule_radius * std::sqrt(normal.x * normal.x + normal.z * normal.z) +
        (normal.y >= 0.0f ? bounds.below : bounds.above) * std::abs(normal.y);
}

inline Vec3 safe_surface_destination(Vec3 point, Vec3 normal, CollisionBounds bounds) {
    normal = normalized(normal);
    return point + normal * capsule_support_distance(normal, bounds);
}

inline bool collision_clearance_route(
    Vec3 start, Vec3 end, std::span<const std::array<Vec3, 3>> contacts,
    CollisionBounds bounds, Vec3& lift, Vec3& cross_over
) {
    const Vec3 horizontal = normalized({end.x - start.x, 0.0f, end.z - start.z});
    if (length(horizontal) < 0.99f || contacts.empty()) { return false; }
    float clearance_y = start.y;
    float crossing_distance = 0.0f;
    for (const auto& triangle : contacts) {
        const Vec3 normal = normalized(cross(triangle[1] - triangle[0], triangle[2] - triangle[0]));
        const float approach = dot(horizontal, normal);
        if (std::abs(normal.y) >= 0.75f || std::abs(approach) < 0.0001f) { continue; }
        const float plane_distance = dot(triangle[0] - start, normal) / approach;
        if (!std::isfinite(plane_distance) || plane_distance < 0.0f) { continue; }
        for (const Vec3 point : triangle) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) { return false; }
            clearance_y = std::max(clearance_y, point.y + bounds.below + arrival_tolerance);
        }
        crossing_distance = std::max(crossing_distance, plane_distance);
    }
    if (!std::isfinite(clearance_y) || !std::isfinite(crossing_distance) ||
            crossing_distance <= 0.0f || clearance_y <= start.y) { return false; }

    lift = {start.x, clearance_y, start.z};
    for (const auto& triangle : contacts) {
        const Vec3 normal = normalized(cross(triangle[1] - triangle[0], triangle[2] - triangle[0]));
        const float approach = dot(horizontal, normal);
        if (std::abs(normal.y) >= 0.75f || std::abs(approach) < 0.0001f ||
                dot(triangle[0] - start, normal) / approach < 0.0f) { continue; }

        const Vec3 facing = approach < 0.0f ? normal : normal * -1.0f;
        crossing_distance = std::max(crossing_distance,
            dot(triangle[0] - lift, normal) / approach +
            (capsule_support_distance(facing * -1.0f, bounds) + arrival_tolerance) /
                std::abs(approach));
    }
    if (!std::isfinite(crossing_distance)) { return false; }
    cross_over = lift + horizontal * crossing_distance;
    return true;
}

inline Vec3 orient_surface_normal(Vec3 normal, Vec3 origin, Vec3 point) {
    normal = normalized(normal);
    return dot(normal, origin - point) < 0.0f ? normal * -1.0f : normal;
}

inline Vec3 advance_towards(
    Vec3 from,
    Vec3 to,
    float maximum
) {
    const Vec3 delta = to - from;
    const float distance = length(delta);
    if (!std::isfinite(distance) || distance <= 0.0f || maximum <= 0.0f) {
        return {};
    }
    return delta * (std::min(maximum, distance) / distance);
}

inline Vec3 advance_along_segment(
    Vec3 start,
    Vec3 current,
    Vec3 end,
    float maximum
) {
    const Vec3 cable = end - start;
    const float cable_length = length(cable);
    if (!std::isfinite(cable_length) || cable_length <= 0.0001f) {
        return {};
    }
    const Vec3 direction = cable * (1.0f / cable_length);
    const float progress = std::clamp(
        dot(current - start, direction), 0.0f, cable_length);
    const Vec3 next = start + direction * std::min(
        progress + maximum, cable_length);
    return advance_towards(current, next, maximum);
}

inline float distance_from_cable(Vec3 start, Vec3 current, Vec3 end) {
    const Vec3 cable = end - start;
    const float cable_length = length(cable);
    if (!std::isfinite(cable_length) || cable_length <= 0.0001f) {
        return length(current - start);
    }
    const Vec3 direction = cable * (1.0f / cable_length);
    const float along = std::clamp(
        dot(current - start, direction), 0.0f, cable_length);
    return length(current - (start + direction * along));
}

inline float travel_progress(Vec3 start, Vec3 current, Vec3 end) {
    const float total = length(end - start);
    return total <= 0.0001f
        ? 1.0f
        : std::clamp(length(current - start) / total, 0.0f, 1.0f);
}

inline Vec3 flight_position(
    Vec3 start,
    Vec3 end,
    uint16_t update,
    uint16_t duration
) {
    if (duration == 0 || update >= duration) {
        return end;
    }
    return start + (end - start) *
        (static_cast<float>(update) / static_cast<float>(duration));
}

inline bool rdram_range_valid(uint32_t address, size_t size) {
    const uint32_t segment = address >> 29;
    const uint32_t offset = address & 0x1FFFFFFFU;
    return (segment == 4U || segment == 5U) &&
        size <= 0x800000U && offset <= 0x800000U - size;
}

inline uint64_t surface_fingerprint(std::span<const uint8_t> bytes) {
    uint64_t hash = 14695981039346656037ULL;
    for (uint8_t byte : bytes) {
        hash = (hash ^ byte) * 1099511628211ULL;
    }
    return hash;
}

void handle_input(int player, uint16_t& buttons);

void invalidate_presentation();
void publish_world_active(bool active);
bool ready(int player);
bool active(int player);
bool aim_anchor_ready(int player);

bool consume_fall_damage_protection(uint32_t player_object);

}

#endif
