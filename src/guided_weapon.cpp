#include "twine_recomp.h"

extern "C" void twine_guided_target_enabled(uint8_t* rdram, recomp_context* ctx) {

    const uint32_t table = static_cast<uint32_t>(ctx->r30);
    constexpr uint32_t first = 0x800C469CU, stride = 232U, count = 59U;
    if (rdram && table >= first && table < first + count * stride &&
            (table - first) % stride == 0 &&
            (TWINE_MEM_W(0x60, table) & 0x8U) != 0) {
        ctx->r2 = 1;
    }
}
