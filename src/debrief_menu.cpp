#include <cstdint>

#include "recomp.h"

extern "C" void twine_profile_omit_debrief_save(
    uint8_t*,
    recomp_context* ctx
) {
    if (ctx == nullptr || ctx->r2 != 5U) {
        return;
    }

    ctx->r2 = 0U;

}
