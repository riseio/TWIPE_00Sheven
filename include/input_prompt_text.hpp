#ifndef TWINE_INPUT_PROMPT_TEXT_HPP
#define TWINE_INPUT_PROMPT_TEXT_HPP

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include "hud_layout.hpp"
#include "rom_metadata.hpp"

namespace twine::input_prompts {

struct Labels {
    std::string a;
    std::string b;
    std::string accept;
    std::string back;
    std::string z;
    std::string l;
    std::string r;
    std::string toggle_xray;
    std::string start;
    std::string c_up;
    std::string c_down;
    std::string c_left;
    std::string c_right;
    std::string dpad_up;
    std::string dpad_down;
    std::string dpad_left;
    std::string dpad_right;
    std::string stick;
    std::string move_vertical;
    std::string move_horizontal;
    std::string c_buttons;
    std::string dpad;
};

enum class ActionBinding {
    none,
    accept,
    back,
    restart,
    quit,
};

inline std::string_view trim_prompt_text(std::string_view text) {
    while (!text.empty() &&
           static_cast<uint8_t>(text.front()) <= 0x20) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           static_cast<uint8_t>(text.back()) <= 0x20) {
        text.remove_suffix(1);
    }
    return text;
}

inline bool trimmed_equals(std::string_view text, std::string_view expected) {
    return trim_prompt_text(text) == expected;
}

inline ActionBinding classify_action(std::string_view text) {
    uint64_t identity = 14695981039346656037ULL;
    for (const unsigned char value : trim_prompt_text(text))
        identity = (identity ^ value) * 1099511628211ULL;
    switch (identity) {
    case 0xC16E00A7A8B2FDE2ULL:
    case 0x2CEB11BE2290BB1BULL:
    case 0x09205907B5B56CDAULL:
    case 0xF42B08406F609462ULL:
    case 0x5EE26933A140671AULL:
    case 0x8F411426731B6D29ULL:
    case 0xD2792907114A0EACULL:
        return ActionBinding::back;
    case 0xFC63E31AC7C956EDULL:
    case 0x81ECF2D4386B8E84ULL:
    case 0x711A0C3A09CFB1E6ULL:
    case 0x504B17D0D0A7262EULL:
    case 0x2F64461723D3DF5DULL:
    case 0x740D542BBE696DE5ULL:
    case 0x122C4E6C8FF0ED45ULL:
    case 0xD1C2A8480C5DBAD5ULL:
    case 0x33F85F24C0F5F008ULL:
    case 0xCB489A1A173AC3F0ULL:
    case 0x091D5D07B5B33DCFULL:
    case 0x091D3D07B5B3076FULL:
    case 0x5B424851E1043CDDULL:
    case 0xC1F0EAF2390BC10BULL:
    case 0x312F949BDEC3EC02ULL:
    case 0x3D20A6B47FFF4AFBULL:
    case 0x53B3F172EA059E7EULL:
    case 0x46F783615DAB3540ULL:
        return ActionBinding::accept;
    case 0x14BEC4678C4E60CCULL:
    case 0xD902BCB98A5F09B7ULL:
    case 0xAA226D1A31A932A9ULL:
        return ActionBinding::restart;
    case 0x45156213B0FB2396ULL:
    case 0x0D5C7B93414486AAULL:
        return ActionBinding::quit;
    default: return ActionBinding::none;
    }
}

inline ActionBinding resolve_confirmation_action(
    ActionBinding action,
    bool restart_quit_pair
) {
    if (!restart_quit_pair) {
        return action == ActionBinding::restart || action == ActionBinding::quit
            ? ActionBinding::accept
            : action;
    }
    if (action == ActionBinding::restart) {
        return ActionBinding::accept;
    }
    if (action == ActionBinding::quit) {
        return ActionBinding::back;
    }
    return action;
}

struct PromptNode {
    uint32_t address{};
    uint32_t text{};
    int16_t x{};
    int16_t y{};
    uint16_t flags{};
    uint8_t layer{};
    uint8_t alpha{255};
};

inline bool action_visible_for_modal(
    uint8_t node_layer,
    int32_t confirmation_layer
) {
    return confirmation_layer < 0 ||
        node_layer == static_cast<uint8_t>(confirmation_layer);
}

enum class OverlayOrigin : uint8_t {
    Center,
    Left,
    Right,
};

struct OverlayPromptIdentity {
    uint32_t key{};
    int32_t binding_type{};
    int32_t binding_id{};
    int32_t x{};
    int32_t y{};
    OverlayOrigin origin{OverlayOrigin::Center};
    uint8_t alpha{255};
    auto operator<=>(const OverlayPromptIdentity&) const = default;
};

inline bool same_renderable_prompt(
    const OverlayPromptIdentity& left,
    const OverlayPromptIdentity& right
) {

    return left.binding_type == right.binding_type &&
        left.binding_id == right.binding_id;
}

template <size_t Capacity>
struct OverlayFrame {
    std::array<OverlayPromptIdentity, Capacity> prompts{};
    size_t count{};
    auto operator<=>(const OverlayFrame&) const = default;

    bool upsert(const OverlayPromptIdentity& prompt) {
        for (size_t index = 0; index < count; ++index) {
            if (prompts[index].key == prompt.key) {
                prompts[index] = prompt;
                return true;
            }
        }
        if (count >= prompts.size()) {
            return false;
        }
        prompts[count++] = prompt;
        return true;
    }

    bool contains_renderable(const OverlayPromptIdentity& prompt) const {
        for (size_t index = 0; index < count; ++index) {
            if (same_renderable_prompt(prompts[index], prompt)) {
                return true;
            }
        }
        return false;
    }
};

struct OverlayLayout {
    float x{};
    float y{};
    float size{};
};

inline OverlayLayout overlay_layout(
    int window_width,
    int window_height,
    int32_t source_x,
    int32_t source_y,
    OverlayOrigin origin,
    hud::Aspect aspect = hud::Aspect::Expand,
    hud::SafeArea safe = hud::SafeArea::Full
) {
    constexpr float source_icon_size = 16.0f;
    const auto canvas = hud::canvas_for(window_width, window_height, aspect, safe);
    const auto anchor = origin == OverlayOrigin::Left ? hud::Origin::Left :
        origin == OverlayOrigin::Right ? hud::Origin::Right : hud::Origin::None;
    return {
        canvas.left(anchor) + static_cast<float>(source_x) * canvas.scale,
        canvas.top + static_cast<float>(source_y) * canvas.scale,
        source_icon_size * canvas.scale,
    };
}

inline size_t find_icon(
    std::span<const PromptNode> nodes,
    size_t text_index,
    std::span<const bool> used
) {
    constexpr int32_t max_horizontal_gap = 64;
    constexpr int32_t max_vertical_gap = 6;
    if (text_index >= nodes.size() || used.size() != nodes.size() || nodes[text_index].alpha == 0) {
        return nodes.size();
    }

    size_t best = nodes.size();
    uint32_t best_gap = static_cast<uint32_t>(
        max_horizontal_gap + max_vertical_gap + 1);
    const auto consider = [&](size_t candidate) {
        if (used[candidate] ||
                nodes[candidate].alpha == 0 ||
                nodes[candidate].text != 0 ||
                (nodes[candidate].flags & 0x0040U) == 0 ||
                (nodes[candidate].flags & 0x0002U) != 0 ||
                nodes[candidate].layer != nodes[text_index].layer) {
            return;
        }
        const int32_t dx =
            static_cast<int32_t>(nodes[candidate].x) -
            static_cast<int32_t>(nodes[text_index].x);
        const int32_t dy =
            static_cast<int32_t>(nodes[candidate].y) -
            static_cast<int32_t>(nodes[text_index].y);
        const int32_t absolute_dx = dx < 0 ? -dx : dx;
        const int32_t absolute_dy = dy < 0 ? -dy : dy;
        if (absolute_dx > max_horizontal_gap ||
                absolute_dy > max_vertical_gap) {
            return;
        }
        const uint32_t gap =
            static_cast<uint32_t>(absolute_dx + absolute_dy);
        if (gap < best_gap) {
            best = candidate;
            best_gap = gap;
        }
    };

    for (size_t candidate = 0; candidate < nodes.size(); ++candidate) {
        if (candidate != text_index) {
            consider(candidate);
        }
    }
    return best;
}

inline void replace_all(
    std::string& text,
    std::string_view from,
    std::string_view to
) {
    for (size_t position = 0;
         (position = text.find(from, position)) != std::string::npos;
         position += to.size()) {
        text.replace(position, from.size(), to);
    }
}

inline std::string normalize_prompt_whitespace(std::string_view source) {
    std::string normalized;
    normalized.reserve(source.size());
    bool pending_space = false;
    for (const char value : source) {
        if (static_cast<uint8_t>(value) <= 0x20U) {
            pending_space = !normalized.empty();
            continue;
        }
        if (pending_space) {
            normalized.push_back(' ');
            pending_space = false;
        }
        normalized.push_back(value);
    }
    return normalized;
}

inline uint64_t tutorial_identity(std::string_view source) {
    const auto normalized = normalize_prompt_whitespace(source);
    uint64_t identity = 14695981039346656037ULL;
    for (const unsigned char value : normalized) {
        identity = (identity ^ value) * 1099511628211ULL;
    }
    return identity;
}

inline bool is_snowboard_speed_tutorial(std::string_view source) {
    const auto identity = tutorial_identity(source);
    return identity == 0xF3F684480438C54FULL ||
        identity == 0xF508A64C2A77B260ULL;
}

inline bool is_snowboard_strafe_tutorial(std::string_view source) {
    const auto identity = tutorial_identity(source);
    return identity == 0x38C58182F4A2A83BULL ||
        identity == 0x8B2C038CD4D5ED40ULL;
}

inline bool is_snowboard_combined_tutorial(std::string_view source) {
    const auto identity = tutorial_identity(source);
    return identity == 0xEF30168A5BA48BE3ULL ||
        identity == 0x25C387630304AF1FULL;
}

inline bool is_snowboard_tutorial(std::string_view source) {
    return is_snowboard_speed_tutorial(source) ||
        is_snowboard_strafe_tutorial(source) ||
        is_snowboard_combined_tutorial(source);
}

inline bool is_night_vision_tutorial(std::string_view source) {
    const auto identity = tutorial_identity(source);
    return identity == 0x77A385CB344E9FD2ULL;
}

inline bool is_xray_tutorial(std::string_view source) {
    const auto identity = tutorial_identity(source);
    return identity == 0x1E892F9FB895EAEFULL;
}

inline bool is_contextual_tutorial(std::string_view source) {
    return is_snowboard_tutorial(source) ||
        is_night_vision_tutorial(source) || is_xray_tutorial(source);
}

inline std::string rewrite(std::string_view source, const Labels& labels) {
    std::string text(source);
    const std::string c_vertical = labels.c_up + "/" + labels.c_down;
    const std::string c_horizontal = labels.c_left + "/" + labels.c_right;
    const std::string dpad_vertical =
        labels.dpad_up + "/" + labels.dpad_down;
    const std::string dpad_horizontal =
        labels.dpad_left + "/" + labels.dpad_right;

    if (is_snowboard_speed_tutorial(source)) {
        return "Use " + labels.move_vertical +
            " to adjust speed.";
    }
    if (is_snowboard_strafe_tutorial(source)) {
        return "Use " + labels.move_horizontal + " to strafe.";
    }
    if (is_snowboard_combined_tutorial(source)) {
        return "Use " + labels.move_vertical +
            " to adjust speed. Use " + labels.move_horizontal +
            " to strafe.";
    }
    if (is_night_vision_tutorial(source)) {
        return "Press " + labels.toggle_xray +
            " to switch night vision on or off. You can also use the gadget wheel.";
    }
    if (is_xray_tutorial(source)) {
        return "Press " + labels.toggle_xray +
            " to switch X-ray vision on or off. You can also use the gadget wheel.";
    }

    const auto& tokens = rom::metadata().text;
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_vertical_1)], c_vertical);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_vertical_2)], c_vertical);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_horizontal_1)], c_horizontal);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::dpad_vertical_1)], dpad_vertical);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::dpad_horizontal_1)], dpad_horizontal);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::dpad_vertical_2)], dpad_vertical);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::dpad_horizontal_2)], dpad_horizontal);

    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_horizontal_2)], c_horizontal);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_vertical_3)], c_vertical);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_horizontal_3)], c_horizontal);
    replace_all(text, "Auf/Ab C-Tasten", c_vertical);

    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_up_1)], labels.c_up);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_down_1)], labels.c_down);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_left_1)], labels.c_left);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_right_1)], labels.c_right);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_up_2)], labels.c_up);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_up_3)], labels.c_up);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_down_2)], labels.c_down);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_left_2)], labels.c_left);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_right_2)], labels.c_right);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_up_4)], labels.c_up);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_down_3)], labels.c_down);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_left_3)], labels.c_left);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_right_3)], labels.c_right);

    replace_all(text, tokens[static_cast<size_t>(rom::Text::dpad_up_1)], labels.dpad_up);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::dpad_down_1)], labels.dpad_down);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::dpad_left_1)], labels.dpad_left);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::dpad_right_1)], labels.dpad_right);

    replace_all(text, tokens[static_cast<size_t>(rom::Text::a_1)], labels.a);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::b_1)], labels.b);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::z_1)], labels.z);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::l_1)], labels.l);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::r_1)], labels.r);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::a_2)], labels.a);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::b_2)], labels.b);
    replace_all(text, "Bouton Z", labels.z);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::l_2)], labels.l);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::r_2)], labels.r);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::a_3)], labels.a);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::b_3)], labels.b);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::z_2)], labels.z);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::l_3)], labels.l);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::r_3)], labels.r);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::a_4)], labels.a);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::b_4)], labels.b);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::z_3)], labels.z);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::l_4)], labels.l);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::r_4)], labels.r);

    replace_all(text, tokens[static_cast<size_t>(rom::Text::start_1)], labels.start);

    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_buttons_1)], labels.c_buttons);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_buttons_2)], labels.c_buttons);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::c_buttons_3)], labels.c_buttons);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::dpad_1)], labels.dpad);
    replace_all(text, tokens[static_cast<size_t>(rom::Text::stick_1)], labels.stick);
    return text;
}

}

#endif
