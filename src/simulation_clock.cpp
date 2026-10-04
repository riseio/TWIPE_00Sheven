#include "twine_recomp.h"
#include "simulation_clock.hpp"
#include "ultramodern/ultramodern.hpp"

namespace {

using twine::simulation::TicksPerVI;
thread_local uint64_t sampledSimulationTime = 0;

void return_time(recomp_context* ctx, uint64_t time) {
    ctx->r2 = int32_t(time >> 32);
    ctx->r3 = int32_t(time);
}
}

extern "C" void twine_initialize_simulation_period(uint8_t* rdram, recomp_context*) {

    TWINE_MEM_W(0, 0x80108F88U) = 0;
    TWINE_MEM_W(0, 0x80108F8CU) = uint32_t(TicksPerVI);
}

extern "C" void twine_sample_simulation_time(uint8_t*, recomp_context* ctx) {
    sampledSimulationTime = ultramodern::get_vi_sequence() * TicksPerVI;
    return_time(ctx, sampledSimulationTime);
}

extern "C" void twine_commit_simulation_time(uint8_t*, recomp_context* ctx) {

    return_time(ctx, sampledSimulationTime);
}
