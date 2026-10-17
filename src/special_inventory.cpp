#include "twine_recomp.h"

namespace {
constexpr uint32_t equipment_metadata = 0x800C4480U;
constexpr uint32_t equipment_count = 29;
constexpr uint32_t item_count = 59;

int backing_item(uint8_t* rdram, uint32_t id) {
    return id > 0 && id < equipment_count
        ? TWINE_MEM_H(6, equipment_metadata + id * 16U) : -1;
}
}

extern "C" void twine_preserve_shared_equipment(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx) { return; }
    const uint32_t inventory = uint32_t(ctx->r4);
    if ((inventory & 3U) || inventory < 0x80000000U || inventory > 0x80800000U - 0x410U) {
        return;
    }
    const int item = backing_item(rdram, uint32_t(ctx->r5) & 0xFFU);
    if (item < 0 || uint32_t(item) >= item_count) { return; }

    for (uint32_t slot = 1; slot < 30; ++slot) {
        const uint16_t record = TWINE_MEM_HU(0x138U + slot * 2U, inventory);
        if ((record >> 8) == 1 && backing_item(rdram, record & 0xFFU) == item) {
            TWINE_MEM_B(0x1FCU + uint32_t(item) * 8U, inventory) = 1;
            return;
        }
    }
}
