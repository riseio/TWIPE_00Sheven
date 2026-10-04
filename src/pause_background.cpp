#include "pause_background.hpp"

#include <array>
#include <algorithm>
#include "hud_layout.hpp"
#include "twine_recomp.h"

namespace {
struct Patch { uint32_t node; uint32_t metadata; };
thread_local std::array<Patch, 7> patches{};
thread_local size_t count = 0;
}

void twine::pause_background::finish(uint8_t* rdram) {
    for (size_t i = 0; i < count; ++i) {
        TWINE_MEM_W(0x28, patches[i].node) = patches[i].metadata;
    }
    count = 0;
}

void twine::pause_background::prepare(uint8_t* rdram, uint32_t root) {
    finish(rdram);
    if (!rdram || !hud::native_range(root, 8)) { return; }
    const uint32_t menu = TWINE_MEM_W(0, 0x80102F2CU);
    const uint32_t manager = TWINE_MEM_W(0, 0x80109BB0U);
    if (!hud::native_range(menu, 0x80) || !hud::native_range(manager, 0x34) ||
            uint32_t(TWINE_MEM_W(0x6C, menu)) != manager) { return; }
    const uint32_t table = TWINE_MEM_W(0xC, manager);
    if (table != 0x800BDB1CU && table != 0x800BDBF8U) { return; }
    const uint32_t background = TWINE_MEM_W(0x20, manager);
    if (!hud::native_range(background, 0x80) ||
            TWINE_MEM_HU(0x78, background) != 0x37) { return; }
    const uint32_t state = TWINE_MEM_W(0x6C, background);
    if (!hud::native_range(state, 0x28)) { return; }

    uint32_t node = TWINE_MEM_W(4, root);
    for (unsigned visited = 0; visited < 256 && hud::native_range(node, 0x2C); ++visited) {
        for (uint32_t offset = 0xC; offset <= 0x24; offset += 4) {
            if (uint32_t(TWINE_MEM_W(offset, state)) == node && count < patches.size()) {
                if (std::any_of(patches.begin(), patches.begin() + count,
                        [node](const Patch& patch) { return patch.node == node; })) { break; }
                const uint32_t metadata = TWINE_MEM_W(0x28, node);
                patches[count++] = {node, metadata};
                TWINE_MEM_W(0x28, node) = (metadata & ~0xFCU) | 0xFCU;
                break;
            }
        }
        const uint32_t next = TWINE_MEM_W(4, node);
        if (next == node) { break; }
        node = next;
    }
}
