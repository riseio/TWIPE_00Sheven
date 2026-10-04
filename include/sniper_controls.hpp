#ifndef TWINE_SNIPER_CONTROLS_HPP
#define TWINE_SNIPER_CONTROLS_HPP

#include <array>
#include <cstdint>

namespace twine::sniper {

inline constexpr uint8_t deutsche_sa90 = 23;
inline constexpr uint8_t suisse_ssr_4000 = 22;

constexpr bool owns_scope_action(uint8_t item) {
    return item == deutsche_sa90 || item == suisse_ssr_4000;
}

constexpr uint16_t apply_scope_toggle(
    uint8_t item,
    uint16_t flags,
    bool cycle_mode_requested
) {
    return owns_scope_action(item) && cycle_mode_requested
        ? static_cast<uint16_t>(flags ^ 1U)
        : flags;
}

constexpr uint16_t apply_scope_latch(
    uint16_t flags,
    bool owns_scope,
    bool scoped,
    bool aim_held = false
) {
    if (aim_held) {
        return static_cast<uint16_t>(flags | 1U);
    }
    if (!owns_scope) {
        return flags;
    }
    return scoped
        ? static_cast<uint16_t>(flags | 1U)
        : static_cast<uint16_t>(flags & ~1U);
}

struct ScopeIntent {
    uint64_t epoch = 0;
    uint8_t item = 0;
    bool owns_scope = false;
    bool scoped = false;

    std::array<uint8_t, 2> modes{};

    bool select(uint64_t next_epoch, uint8_t next_item, bool remember) {
        if (epoch != next_epoch) { *this = {next_epoch}; }
        if (!remember) { modes = {}; }
        else { remember_current(); }
        if (!owns_scope_action(next_item)) {
            item = next_item;
            owns_scope = scoped = false;
            return false;
        }
        if (item != next_item) {
            item = next_item;
            const uint8_t mode = modes[item == deutsche_sa90 ? 1 : 0];

            owns_scope = owns_scope || mode != 0;
            scoped = mode == 2;
        }
        return true;
    }

    void remember_current() {
        if (owns_scope_action(item) && owns_scope) {
            modes[item == deutsche_sa90 ? 1 : 0] = scoped ? 2 : 1;
        }
    }

    void toggle(uint16_t flags, bool remember) {
        if (!owns_scope_action(item)) return;
        scoped = owns_scope ? !scoped : (flags & 1U) == 0;
        owns_scope = true;
        if (remember) { remember_current(); }
    }
};

struct ZoomInput {
    bool owns_vertical = false;
    bool in = false;
    bool out = false;
    float forward = 0.0f;
    bool consume_in = false;
    bool consume_out = false;
};

constexpr ZoomInput route_zoom(bool zoom_available, bool scoped, bool aim_held,
        bool zoom_in, bool zoom_out, float forward) {
    const bool owns = zoom_available && (scoped || aim_held);
    const bool stick_zoom = zoom_available && aim_held;
    return {owns, owns && (zoom_in || (stick_zoom && forward > 0.25f)),
        owns && (zoom_out || (stick_zoom && forward < -0.25f)),
        stick_zoom ? 0.0f : forward, owns && zoom_in, owns && zoom_out};
}

struct ZoomRouting {
    uint64_t epoch = 0;
    bool in_claimed = false;
    bool out_claimed = false;

    ZoomInput update(uint64_t next_epoch, bool zoom_available, bool scoped, bool aim_held,
            bool zoom_in, bool zoom_out, float forward) {
        if (epoch != next_epoch) { *this = {next_epoch}; }
        auto result = route_zoom(zoom_available, scoped, aim_held, zoom_in, zoom_out, forward);

        in_claimed = zoom_in && (in_claimed || result.owns_vertical);
        out_claimed = zoom_out && (out_claimed || result.owns_vertical);
        result.consume_in = in_claimed;
        result.consume_out = out_claimed;
        return result;
    }
};

constexpr float zoom_axis(bool zoom_in, bool zoom_out) {
    return zoom_in == zoom_out ? 0.0f : (zoom_in ? -1.0f : 1.0f);
}

inline float look_scale(float zoom) {
    return zoom > 1.0f ? 1.0f / zoom : 1.0f;
}

constexpr float idle_sway_amplitude(
    uint8_t item,
    bool scoped,
    float stock_amplitude
) {
    return owns_scope_action(item) && scoped ? 0.0f : stock_amplitude;
}

}

#endif
