#include "modern_grapple.hpp"
#include "twine_recomp.h"

extern "C" void twine_native_ladder_launch(uint8_t*, recomp_context*);

extern "C" void twine_ladder_jump(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t inventory = uint32_t(ctx->r17);
    const uint32_t actor = uint32_t(ctx->r18);
    if (!twine::grapple::rdram_range_valid(inventory, 0x188) ||
            !twine::grapple::rdram_range_valid(actor, 0x80) ||
            TWINE_MEM_HU(0x7C, actor) != 0 ||
            TWINE_MEM_BU(0x181, inventory) != 4) { return; }
    const uint32_t item = TWINE_MEM_W(0x68, actor);
    if (!twine::grapple::rdram_range_valid(item, 0x10)) return;

    recomp_context launch = *ctx;
    launch.f_odd = launch.mips3_float_mode ? &launch.f1.u32l : &launch.f0.u32h;
    twine_native_ladder_launch(rdram, &launch);
    TWINE_MEM_B(0x186, inventory) = 18;
}
