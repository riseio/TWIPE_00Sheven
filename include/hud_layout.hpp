#ifndef TWINE_HUD_LAYOUT_HPP
#define TWINE_HUD_LAYOUT_HPP

#include <cstdint>
#include <algorithm>
#include <array>
#include <compare>

namespace twine::hud {

enum class Origin : uint8_t { None, Left, Right };

inline constexpr int16_t fixed_x_offset_for(Origin origin) {

    return origin == Origin::Right ? -320 * 4 : 0;
}

inline constexpr bool mission_ui_context(
    int16_t overlay,
    uint8_t demo_mode,
    bool native_menu_active
) {

    return overlay > 1 && demo_mode == 0 && !native_menu_active;
}

inline Origin origin_for(int x, uint16_t  ) {

    if (x <= 80) {
        return Origin::Left;
    }
    if (x >= 240) {
        return Origin::Right;
    }
    return Origin::None;
}

inline constexpr bool native_range(uint32_t address, uint32_t size) {
    const uint32_t segment = address & 0xE0000000U;
    const uint32_t physical = address & 0x1FFFFFFFU;
    return (segment == 0x80000000U || segment == 0xA0000000U) &&
        (address & 3U) == 0 && size <= 0x800000U &&
        physical <= 0x800000U - size;
}

enum class Corner : uint8_t { None, TopLeft, TopRight, BottomLeft, BottomRight };

template <class ReadWord>
Corner widget_corner(uint32_t node, ReadWord read_word) {
    if (!native_range(node, 0x2CU)) {
        return Corner::None;
    }
    struct Component { uint32_t offset; Corner corner; };
    constexpr std::array components{

        Component{0x404, Corner::TopRight}, Component{0x408, Corner::TopRight},
        Component{0x40C, Corner::TopRight},

        Component{0x478, Corner::TopLeft}, Component{0x47C, Corner::TopLeft},
        Component{0x480, Corner::TopLeft}, Component{0x484, Corner::TopLeft},
        Component{0x488, Corner::TopLeft},

        Component{0x418, Corner::BottomRight}, Component{0x41C, Corner::BottomRight},
        Component{0x420, Corner::BottomRight}, Component{0x424, Corner::BottomRight},
        Component{0x428, Corner::BottomRight},
        Component{0x490, Corner::BottomLeft},
    };
    for (uint32_t player = 0; player < 4; ++player) {

        const uint32_t entity = read_word(0x8010A500U + player * 4);
        const uint32_t state = read_word(0x80102CD0U + player * 4);
        if (!native_range(entity, 0x70) || !native_range(state, 0x494) ||
                read_word(entity + 0x6C) != state) {
            continue;
        }
        for (const Component& component : components) {
            const uint32_t owned = read_word(state + component.offset);
            if (native_range(owned, 0x2C) &&
                    (owned & 0x1FFFFFFFU) == (node & 0x1FFFFFFFU)) {
                return component.corner;
            }
        }
    }
    return Corner::None;
}

struct Placement {
    Origin origin = Origin::None;
    int x = 0;
    int y = 0;
};

template <class ReadWord>
Placement node_placement(uint32_t node, int x, uint16_t flags, bool expanded, ReadWord read_word) {
    if (!native_range(node, 0x2C)) return {};
    const auto corner = widget_corner(node, read_word);
    const bool left = corner == Corner::TopLeft || corner == Corner::BottomLeft;
    const bool right = corner == Corner::TopRight || corner == Corner::BottomRight;
    Placement result{left ? Origin::Left : right ? Origin::Right : origin_for(x, flags)};

    if (expanded && corner != Corner::None && read_word(0x80102ED8U) == 0) {
        constexpr int trim = 12;
        result.x = left ? -trim : trim;
        result.y = corner == Corner::TopLeft || corner == Corner::TopRight ? -trim : trim;
    }
    return result;
}

template <class ReadWord>
Origin node_origin(uint32_t node, int x, uint16_t flags, ReadWord read_word) {
    return node_placement(node, x, flags, false, read_word).origin;
}

enum class Aspect { Original, Expand, Wide };
enum class SafeArea { Original, Clamp16x9, Full };

struct Canvas {
    float scale{};
    float center_left{};
    float top{};
    float edge_shift{};
    auto operator<=>(const Canvas&) const = default;

    float left(Origin origin) const {
        return center_left + (origin == Origin::Left ? -edge_shift :
            origin == Origin::Right ? edge_shift : 0.0f);
    }
};

inline Canvas canvas_for(int width, int height, Aspect aspect, SafeArea safe) {
    if (width <= 0 || height <= 0) {
        return {};
    }

    constexpr float wide = 16.0f / 9.0f;
    const float scale = std::min(float(height) / 240.0f,
        float(width) / (aspect == Aspect::Wide ? 240.0f * wide : 320.0f));
    const float output_height = scale * 240.0f;
    const float native_width = 320.0f * scale;
    const float output_width = aspect == Aspect::Expand ? float(width) :
        aspect == Aspect::Wide ? output_height * wide : native_width;
    const float hud_width = safe == SafeArea::Original ? native_width :
        safe == SafeArea::Clamp16x9 ? std::min(output_height * wide, output_width) : output_width;
    return {scale, (float(width) - 320.0f * scale) * 0.5f,
        (float(height) - output_height) * 0.5f,
        (hud_width - native_width) * 0.5f};
}

}

#endif
