#include "twine_recomp.h"
#include "recompinput/input_state.h"

extern "C" void func_80010690(uint8_t*, recomp_context*);

extern "C" void twine_prepare_controller_feedback(uint8_t* rdram, recomp_context* ctx) {
    const unsigned player = uint32_t(ctx->r4) & 0xFFFFU;
    if (!rdram || player >= 4) { return; }
    const uint32_t availability = 0x80109DC8U + player * 2;
    if (!recompinput::has_rumble(static_cast<int>(player))) {
        TWINE_MEM_H(0, availability) = 0;
        return;
    }
    if (TWINE_MEM_HU(0, availability)) { return; }

    recomp_context motor = *ctx;
    motor.f_odd = motor.mips3_float_mode ? &motor.f1.u32l : &motor.f0.u32h;
    TWINE_MEM_H(0, availability) = 1;
    func_80010690(rdram, &motor);
}
