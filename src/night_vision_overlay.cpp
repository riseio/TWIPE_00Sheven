#include "night_vision_overlay.hpp"
#include <algorithm>
#include <array>
#include "hud_layout.hpp"
#include "twine_recomp.h"

namespace twine::night_vision_overlay {
namespace {
struct Patch { uint32_t address, metadata; };
thread_local std::array<Patch, 12> patches{};
thread_local size_t patch_count = 0;
constexpr uint32_t layer_mask = 0xFCU;
}
void finish(uint8_t* rdram) {
    if (rdram) {
        for (size_t i = 0; i < patch_count; ++i)
            TWINE_MEM_W(0x28, patches[i].address) = patches[i].metadata;
    }
    patch_count = 0;
}
void prepare(uint8_t* rdram, uint32_t root, bool mission_ui) {
    finish(rdram);
    if (!rdram || !mission_ui || !hud::native_range(root, 0x104)) return;
    const uint32_t view_flags = TWINE_MEM_W(0x100, root) & 6U;
    if (!view_flags) return;
    uint32_t inventory = 0;
    for (uint32_t player = 0; player < 4; ++player) {
        if (uint32_t(TWINE_MEM_W(player * 4, 0x80109688U)) != root) continue;
        const uint32_t actor = TWINE_MEM_W(player * 4, 0x8010A500U);
        const uint32_t state = TWINE_MEM_W(player * 4, 0x80102CD0U);
        if (hud::native_range(actor, 0x80) && hud::native_range(state, 0x404) &&
                uint32_t(TWINE_MEM_W(0x6C, actor)) == state &&
                TWINE_MEM_BU(0x182, state) == player) inventory = state;
        break;
    }
    if (!inventory) return;

    std::array<uint32_t, 12> owned{};
    const size_t count = (view_flags & 2U) ? owned.size() : 4;
    for (size_t i = 0; i < count; ++i)
        owned[i] = TWINE_MEM_W(0x3D4 + uint32_t(i) * 4, inventory);
    std::sort(owned.begin(), owned.begin() + count);

    uint32_t node = TWINE_MEM_W(4, root);
    for (unsigned visited = 0; visited < 256 && hud::native_range(node, 0x2C); ++visited) {
        if (std::binary_search(owned.begin(), owned.begin() + count, node) &&
                patch_count < patches.size() &&
                std::none_of(patches.begin(), patches.begin() + patch_count,
                    [node](const Patch& p) { return p.address == node; })) {
            const uint32_t metadata = TWINE_MEM_W(0x28, node);
            patches[patch_count++] = {node, metadata};
            TWINE_MEM_W(0x28, node) = (metadata & ~layer_mask) | layer_mask;
        }
        const uint32_t next = TWINE_MEM_W(4, node);
        if (next == node) break;
        node = next;
    }
}
}
