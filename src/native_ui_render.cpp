#include "native_ui_render.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "hud_layout.hpp"
#include "native_render_commands.hpp"
#include "night_vision_overlay.hpp"
#include "pause_background.hpp"
#include "rt64_extended_gbi.h"
#include "twine_recomp.h"

namespace {
using twine::render::Command;
constexpr uint32_t extended = RT64_EXTENDED_OPCODE << 24;
constexpr uint32_t cursor_address = 0x80115328U;
constexpr uint32_t none_pair = G_EX_ORIGIN_NONE | (G_EX_ORIGIN_NONE << 12);
constexpr Command enable{0xE0525464U, 0x10000064U};
struct Scope {
    uint32_t begin = 0, node_begin = 0, node = 0, root = 0;
    int x = 0;
    bool active = false, wide = false, expanded_hud = false;
};
thread_local Scope scope;

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "Native UI rendering failed: %s\n", message);
    std::abort();
}

uint32_t fixed_pair(int x, int y) {
    return (uint32_t(uint16_t(x)) << 16) | uint16_t(y);
}

uint32_t rt_origin(twine::hud::Origin origin) {
    switch (origin) {
    case twine::hud::Origin::Left: return G_EX_ORIGIN_LEFT;
    case twine::hud::Origin::Right: return G_EX_ORIGIN_RIGHT;
    default: return G_EX_ORIGIN_NONE;
    }
}
}

void twine::render::ui::begin(uint8_t* rdram, uint32_t root, bool mission, bool frontend, bool expanded_hud) try {
    if (scope.active) { throw std::runtime_error("Nested native UI traversal"); }
    night_vision_overlay::prepare(rdram, root, mission);
    pause_background::prepare(rdram, root);
    scope = {};
    scope.root = root;
    scope.active = mission || frontend;
    scope.wide = mission;
    scope.expanded_hud = mission && expanded_hud;
    scope.begin = uint32_t(TWINE_MEM_W(0, cursor_address));
} catch (const std::exception& error) { fail(error.what()); }

extern "C" uint32_t twine_ui_widescreen_alignment_enabled(void) {
    return scope.active && scope.wide;
}

extern "C" void twine_align_ui_node(uint8_t* rdram, recomp_context* ctx) try {
    if (!scope.active) { return; }
    scope.node = uint32_t(ctx->r16);
    if (!twine::hud::native_range(scope.node, 0x2C)) {
        throw std::runtime_error("Invalid native UI node");
    }
    scope.x = int32_t(ctx->r23) + int16_t(TWINE_MEM_H(0x20, scope.node));
    scope.node_begin = uint32_t(TWINE_MEM_W(0, cursor_address));
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_finish_ui_node(uint8_t* rdram, recomp_context*) try {
    if (!scope.active || scope.node_begin == 0) { return; }
    const uint32_t begin = scope.node_begin;
    scope.node_begin = 0;
    const uint32_t end = uint32_t(TWINE_MEM_W(0, cursor_address));
    if (begin == end) { return; }
    const uint16_t flags = TWINE_MEM_HU(0x1E, scope.node);
    const auto placement = scope.wide ? twine::hud::node_placement(scope.node, scope.x, flags,
        scope.expanded_hud, [rdram](uint32_t address) { return uint32_t(TWINE_MEM_W(0, address)); }) :
        twine::hud::Placement{};

    const uint32_t metadata = TWINE_MEM_W(0x28, scope.node);
    const int width = TWINE_MEM_HU(0x26, scope.node) *
        std::max(1U, (metadata >> 12) & 0xFU);
    const bool screen = TWINE_MEM_W(0x18, scope.node) == 0 &&
        scope.x <= 0 && scope.x + width >= 320 && (flags & 0x3000U) == 0;
    const uint32_t anchor = screen ? G_EX_ORIGIN_NONE : rt_origin(placement.origin);
    const int offset = screen ? 0 : twine::hud::fixed_x_offset_for(placement.origin) + placement.x * 4;
    const int offset_y = screen ? 0 : placement.y * 4;
    const auto identity = (flags & 0x3000U) ? 0U : twine::render::ui_identity(
        scope.node, scope.root, uint32_t(TWINE_MEM_W(0x18, scope.node)));
    std::array<Command, 8> commands{{
        {extended | G_EX_SETRECTGROUP_V1, identity},
        {extended | G_EX_SETRECTASPECT_V1, uint32_t(screen ? G_EX_ASPECT_STRETCH : G_EX_ASPECT_ADJUST)},
        {extended | G_EX_SETRECTALIGN_V1, anchor | (anchor << 12)},
        {fixed_pair(offset, offset_y), fixed_pair(offset, offset_y)},
        {extended | G_EX_SETVIEWPORTALIGN_V1, anchor},
        {fixed_pair(offset, offset_y), 0},
        {}, {}
    }};
    size_t count = 6;
    if (scope.wide && (flags & 0x3000U)) {

        const uint32_t w0 = uint32_t(TWINE_MEM_W(0, begin));
        const uint32_t w1 = uint32_t(TWINE_MEM_W(4, begin));
        if (end != begin + 8 || (w0 >> 24) != 0xED) {
            throw std::runtime_error("Native UI scissor writer changed");
        }
        const int left = (w0 >> 12) & 0xFFF, right = (w1 >> 12) & 0xFFF;
        const bool spans_canvas = left == 0 && right == 320 * 4;
        const uint32_t left_origin = spans_canvas ? G_EX_ORIGIN_LEFT : rt_origin(placement.origin);
        const uint32_t right_origin = spans_canvas ? G_EX_ORIGIN_RIGHT : rt_origin(placement.origin);
        const int left_offset = spans_canvas ? 0 : offset;
        const int right_offset = spans_canvas ? -320 * 4 : offset;
        commands[count++] = {extended | G_EX_SETSCISSOR_V1,
            ((w1 >> 24) & 3U) | (left_origin << 2) | (right_origin << 14)};
        commands[count++] = {fixed_pair(left + left_offset, w0 & 0xFFF),
            fixed_pair(right + right_offset, w1 & 0xFFF)};
        TWINE_MEM_W(0, begin) = 0;
        TWINE_MEM_W(4, begin) = 0;
    }

    twine::render::prefix_command(rdram, begin, {commands.data(), count});
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_end_ui_aspect(uint8_t* rdram, recomp_context*) try {
    twine::night_vision_overlay::finish(rdram);
    twine::pause_background::finish(rdram);
    if (!scope.active) { return; }
    if (scope.node_begin != 0) { throw std::runtime_error("Unfinished native UI node"); }
    constexpr std::array prefix{
        enable,
        Command{extended | G_EX_SETRECTASPECT_V1, G_EX_ASPECT_ADJUST},
        Command{extended | G_EX_PUSHSCISSOR_V1, 0},
        Command{extended | G_EX_SETSCISSOR_V1, G_EX_ORIGIN_RIGHT << 14},
        Command{0, 240 * 4}
    };
    constexpr std::array suffix{
        Command{extended | G_EX_SETRECTGROUP_V1, 0},
        Command{extended | G_EX_SETRECTASPECT_V1, G_EX_ASPECT_AUTO},
        Command{extended | G_EX_SETRECTALIGN_V1, none_pair}, Command{0, 0},
        Command{extended | G_EX_SETVIEWPORTALIGN_V1, G_EX_ORIGIN_NONE}, Command{0, 0},
        Command{extended | G_EX_POPSCISSOR_V1, 0}
    };
    twine::render::wrap_commands(rdram, scope.begin, uint32_t(TWINE_MEM_W(0, cursor_address)),
        {prefix.data(), scope.wide ? prefix.size() : 2},
        {suffix.data(), scope.wide ? suffix.size() : suffix.size() - 1});
    scope = {};
} catch (const std::exception& error) { fail(error.what()); }
