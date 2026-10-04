#include "save_state_rendezvous.hpp"
#include "ultramodern/ultra64.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace twine::state {

RootState capture_root(uint32_t thread, const recomp_context& context,
                      const uint64_t* locals, uint32_t cop1) {
    static_assert(offsetof(recomp_context, f0) == sizeof(uint64_t) * 32);
    static_assert(offsetof(recomp_context, hi) == sizeof(uint64_t) * 64);
    RootState result;
    result.thread = thread;
    std::memcpy(result.gpr.data(), &context.r0, sizeof(result.gpr));
    std::memcpy(result.fpr.data(), &context.f0, sizeof(result.fpr));
    result.hi = context.hi;
    result.lo = context.lo;
    result.status = context.status_reg;
    result.float_mode = context.mips3_float_mode;
    std::copy_n(locals, result.locals.size(), result.locals.begin());
    result.cop1 = cop1;
    return result;
}

void validate_root(const RootState& saved, const RootState& current) {
    const auto valid_address = [](uint32_t address, uint32_t length) {
        return address >= 0x80000000U && uint64_t(address) + length <= 0x80800000ULL;
    };
    if (saved.thread != current.thread || saved.gpr[29] != current.gpr[29] ||
        !valid_address(saved.thread, sizeof(OSThread)) ||
        !valid_address(static_cast<uint32_t>(saved.gpr[29]), 0x10) ||
        saved.gpr[0] != 0 || saved.float_mode > 1 ||
        ((saved.status >> 26) & 1U) != saved.float_mode ||
        saved.locals[3] > 1 || saved.cop1 > 3) {
        throw std::runtime_error("Save-state root identity, stack or register mode is incompatible");
    }
}

void restore_root(const RootState& saved, recomp_context& context, uint64_t* locals) {
    std::memcpy(&context.r0, saved.gpr.data(), sizeof(saved.gpr));
    std::memcpy(&context.f0, saved.fpr.data(), sizeof(saved.fpr));
    context.hi = saved.hi;
    context.lo = saved.lo;
    context.status_reg = saved.status;
    context.mips3_float_mode = static_cast<uint8_t>(saved.float_mode);
    context.f_odd = saved.float_mode ? &context.f1.u32l : &context.f0.u32h;
    std::copy(saved.locals.begin(), saved.locals.end(), locals);
}
}
