#include "frontend_text.hpp"
#include "twine_recomp.h"

uint32_t twine::frontend::visible_text(
    uint8_t* rdram, uint32_t address, uint32_t resource) {
    if (!hidden_text(resource) || !rdram ||
            address < 0x80000000U || address >= 0x80800000U) {
        return address;
    }
    constexpr uint32_t maximum_length = 2048;
    for (uint32_t i = 0; i < maximum_length && i < 0x80800000U - address; ++i) {
        if (TWINE_MEM_BU(i, address) == 0) { return address + i; }
    }

    return address;
}

extern "C" void twine_filter_frontend_text(
    uint8_t* rdram, recomp_context* ctx, uint32_t resource) {
    ctx->r2 = twine_n64_address(twine::frontend::visible_text(
        rdram, uint32_t(ctx->r2), resource));
}
