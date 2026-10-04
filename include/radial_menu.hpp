#ifndef RADIAL_MENU_HPP
#define RADIAL_MENU_HPP

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace twine::radial {

class PendingSelection {
    std::atomic<uint64_t> command{0};
public:
    void clear() { command.store(0, std::memory_order_release); }
    void queue(uint8_t item, uint64_t epoch) {
        command.store(item != 0xFF && epoch != 0 && epoch <= (UINT64_MAX >> 8)
            ? (epoch << 8) | item : 0, std::memory_order_release);
    }
    uint8_t peek() const {
        const auto value = command.load(std::memory_order_acquire);
        return value ? uint8_t(value) : 0xFF;
    }
    uint8_t take(uint64_t epoch) {
        const auto value = command.exchange(0, std::memory_order_acq_rel);
        return value && (value >> 8) == epoch ? uint8_t(value) : 0xFF;
    }
};

constexpr uint8_t night_vision_special_id = 0x15U;
constexpr uint8_t xray_special_id = 0x1BU;
constexpr uint8_t night_vision_virtual_item = 55U;
constexpr uint8_t xray_virtual_item = 56U;
constexpr uint8_t virtual_item_end = 57U;

constexpr bool is_virtual_special_item(uint8_t item) {
    return item == night_vision_virtual_item || item == xray_virtual_item;
}

constexpr uint8_t special_id_for_virtual_item(uint8_t item) {
    return item == night_vision_virtual_item ? night_vision_special_id :
        item == xray_virtual_item ? xray_special_id : 0xFFU;
}

inline bool owned(uint64_t inventory, uint8_t item) {
    return item < 64 && (inventory & (uint64_t{1} << item)) != 0;
}

inline uint8_t toggle_equipment(uint64_t inventory, uint8_t last_selected) {

    if (is_virtual_special_item(last_selected) && owned(inventory, last_selected)) {
        return last_selected;
    }
    if (owned(inventory, xray_virtual_item)) { return xray_virtual_item; }
    if (owned(inventory, night_vision_virtual_item)) { return night_vision_virtual_item; }
    return 0xFF;
}

struct WatchShortcut {
    uint8_t held = 0;
    uint8_t update(uint8_t down, uint64_t inventory, bool captured) {
        const uint8_t rising = down & ~held;
        held = down;

        if (captured || rising == 0 || (rising & (rising - 1)) != 0) { return 0xFF; }
        const uint8_t item = rising == 1 ? 35 : rising == 2 ? 36 : rising == 4 ? 34 : 0xFF;
        return owned(inventory, item) ? item : 0xFF;
    }
};

inline constexpr uint8_t next_owned(
    uint64_t inventory,
    uint8_t current,
    uint8_t begin,
    uint8_t end
) {
    if (begin >= end || end > 64) {
        return 0xFF;
    }
    const uint8_t start = current >= begin && current < end
        ? static_cast<uint8_t>(current + 1U) : begin;
    for (uint8_t offset = 0; offset < end - begin; ++offset) {
        const uint8_t item = static_cast<uint8_t>(
            begin + (start - begin + offset) % (end - begin));
        if (owned(inventory, item)) {
            return item;
        }
    }
    return 0xFF;
}

inline constexpr bool weapon_selectable(
    uint8_t item,
    uint8_t possession,
    uint16_t metadata_flags,
    bool ammo_available
) {
    constexpr uint8_t watch_begin = 33;
    constexpr uint8_t watch_end = 37;
    constexpr uint8_t weapon_end = 39;
    return item < weapon_end &&
        !(item >= watch_begin && item < watch_end) &&
        possession != 0 && (metadata_flags & 0x8000U) != 0 &&
        ammo_available;
}

inline int sector(float x, float y, size_t count, int previous = -1) {
    const float magnitude = std::hypot(x, y);
    if (count == 0 || count > 64 || !std::isfinite(x) || !std::isfinite(y)) {
        return -1;
    }
    if (magnitude < 0.20f) {
        return -1;
    }
    if (magnitude < 0.35f) {
        return previous >= 0 && previous < static_cast<int>(count)
            ? previous : -1;
    }
    constexpr float tau = 6.2831853071795864769f;
    float angle = std::atan2(x, -y);
    if (angle < 0.0f) {
        angle += tau;
    }
    const float position = angle * static_cast<float>(count) / tau;
    const int candidate = static_cast<int>(std::floor(position + 0.5f)) %
        static_cast<int>(count);
    if (previous < 0 || previous >= static_cast<int>(count) ||
        candidate == previous) {
        return candidate;
    }
    const float previous_center = static_cast<float>(previous);
    float distance = std::fabs(position - previous_center);
    distance = std::min(distance, static_cast<float>(count) - distance);
    return distance < 0.62f ? previous : candidate;
}

struct Point { float x = 0, y = 0; };
struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
    bool contains(Point p) const {
        return width > 0 && height > 0 && p.x >= x && p.y >= y &&
            p.x <= x + width && p.y <= y + height;
    }
};

inline Point item_position(size_t index, size_t count) {
    if (count == 0 || index >= count) { return {0.5f, 0.5f}; }
    const float angle = 6.28318530718f * float(index) / float(count);
    const float radius = count > 20 && (index & 1U) ? 0.31f : 0.42f;
    return {0.5f + std::sin(angle) * radius, 0.5f - std::cos(angle) * radius};
}

inline int pointer_sector(Point pointer, Rect wheel, std::span<const Rect> labels) {
    if (!wheel.contains(pointer) || labels.empty() || labels.size() > 64) { return -1; }
    const float x = (pointer.x - wheel.x - wheel.width * 0.5f) / (wheel.width * 0.42f);
    const float y = (pointer.y - wheel.y - wheel.height * 0.5f) / (wheel.height * 0.42f);
    if (!std::isfinite(x) || !std::isfinite(y) || std::hypot(x, y) < 0.20f) { return -1; }
    int hovered = -1;
    float nearest = INFINITY;
    for (size_t i = 0; i < labels.size(); ++i) {
        if (!labels[i].contains(pointer)) { continue; }
        const auto center = item_position(i, labels.size());
        const float distance = std::hypot(
            (pointer.x - wheel.x) / wheel.width - center.x,
            (pointer.y - wheel.y) / wheel.height - center.y);
        if (distance < nearest) { nearest = distance; hovered = int(i); }
    }

    return hovered >= 0 ? hovered : sector(x, y, labels.size());
}

inline uint64_t pointer_sample(uint32_t session, uint32_t motion, uint8_t item) {
    return (uint64_t(session) << 32) | (uint64_t(motion & 0xFFFFFFU) << 8) | item;
}

struct PointerMotion {
    int x = 0, y = 0, center_x = 0, center_y = 0;
    uint32_t sequence = 0;
    bool centering = false;

    void begin(int actual_x, int actual_y, int target_x, int target_y) {
        x = actual_x;
        y = actual_y;
        center_x = target_x;
        center_y = target_y;
        sequence = 0;
        centering = x != center_x || y != center_y;
    }

    uint32_t sample(int actual_x, int actual_y) {
        if (actual_x == x && actual_y == y) { return sequence; }
        const bool warp = centering && actual_x == center_x && actual_y == center_y;
        centering = false;
        x = actual_x;
        y = actual_y;
        if (!warp) { sequence = sequence % 0xFFFFFFU + 1U; }
        return sequence;
    }
};

inline int pointer_item(uint64_t sample, uint32_t session, std::span<const uint8_t> items) {
    if (session == 0 || uint32_t(sample >> 32) != session) { return -1; }
    const auto it = std::find(items.begin(), items.end(), uint8_t(sample));
    return it == items.end() ? -1 : int(it - items.begin());
}

struct Selection {
    int index = -1;
    Point last_direction{};
    uint64_t last_pointer = 0;
    bool pointer_owner = false;

    void update(Point direction, uint64_t pointer, uint32_t session,
            std::span<const uint8_t> items) {
        const bool directional = std::isfinite(direction.x) && std::isfinite(direction.y) &&
            std::hypot(direction.x, direction.y) >= 0.35f;
        const bool changed_direction = directional &&
            std::hypot(direction.x-last_direction.x, direction.y-last_direction.y) >= 0.05f;
        const bool changed_pointer = pointer != last_pointer && session != 0 &&
            uint32_t(pointer >> 32) == session &&
            ((pointer >> 8) & 0xFFFFFFU) != 0;
        const int hit = pointer_item(pointer, session, items);
        if (changed_direction) { pointer_owner = false; }

        if (changed_pointer) {
            pointer_owner = true;
            index = hit;
        }
        else if (!pointer_owner && directional) {
            index = sector(direction.x, direction.y, items.size(), index);
        }
        last_direction = direction;
        last_pointer = pointer;
        if (index >= int(items.size())) { index = -1; }
    }
};

}

#endif
