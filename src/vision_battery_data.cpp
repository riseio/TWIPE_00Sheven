#include "vision_battery.hpp"

#include <algorithm>
#include "hud_layout.hpp"
#include "twine_recomp.h"

twine::vision_battery::Status twine::vision_battery::read_status(
        uint8_t* rdram, bool gameplay) {
    if (!rdram || !gameplay || TWINE_MEM_W(0, 0x80102ED8U) != 0 ||
            TWINE_MEM_BU(0, 0x800CEEB0U) != 0) { return {}; }
    const uint32_t actor = TWINE_MEM_W(0, 0x8010A500U);
    const uint32_t inventory = TWINE_MEM_W(0, 0x80102CD0U);
    const uint32_t camera = TWINE_MEM_W(0, 0x80109688U);
    if (!hud::native_range(actor, 0x80U) ||
            !hud::native_range(inventory, 0x522U) ||
            !hud::native_range(camera, 0x104U) ||
            uint32_t(TWINE_MEM_W(0x6C, actor)) != inventory ||
            TWINE_MEM_HU(0x78, actor) != 2U ||
            TWINE_MEM_BU(0x182, inventory) != 0) { return {}; }
    const uint32_t flags = TWINE_MEM_W(0x100, camera) & 7U;
    const uint8_t mode = TWINE_MEM_BU(0x185, inventory);
    if (mode == 1 && flags == 2) {
        const unsigned charge = std::min<unsigned>(
            TWINE_MEM_HU(0x520, inventory), full_charge);
        return {Mode::NightVision, static_cast<uint8_t>(charge * 100U / full_charge)};
    }

    if (mode == 2 && flags == 4) { return {Mode::Xray, 100}; }
    return {};
}
