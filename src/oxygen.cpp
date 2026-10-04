#include "cheats.hpp"
#include "twine_recomp.h"

extern "C" void twine_filter_oxygen_depletion(
    uint8_t* rdram, recomp_context* ctx
) {

    if (rdram != nullptr && ctx != nullptr &&
            TWINE_MEM_W(0, 0x80102ED8U) == 0 &&
            twine::cheats::enabled(twine::cheats::selected_mask(),
                twine::cheats::Cheat::InfiniteOxygen)) {
        ctx->r2 = 0x546;
    }
}
