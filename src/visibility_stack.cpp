#include "visibility_stack.hpp"
#include "twine_recomp.h"
#include "librecomp/addresses.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
uint32_t storage = 0, caller = 0, entry = 0;
constexpr uint32_t kseg0 = 0x80000000U;
constexpr uint32_t guard = 0x71574950U;
constexpr uint32_t headroom = 0x40;
[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "Native visibility stack failed: %s\n", message);
    std::abort();
}
}
void twine::render::prepare_visibility_stack(uint32_t address) {
    if (caller || (address & 7U) || address < kseg0 ||
            uint64_t(address - kseg0) + visibility_stack_size > recomp::mem_size)
        fail("invalid task storage or overlapping traversal");
    storage = address;
}
bool twine::render::visibility_stack_contains(uint32_t address, uint32_t size) {
    return caller && address >= storage + 16 &&
        uint64_t(address) + size <= uint64_t(storage) + visibility_stack_size - 16;
}
void twine::render::reset_visibility_stack() {
    if (caller) fail("reset during traversal");
    storage = entry = 0;
}
extern "C" void twine_begin_visibility_stack(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t stack = uint32_t(ctx->r29);
    if (!storage || caller || (stack & 7U) || stack < kseg0 || stack > 0x80800000U - 0x30)
        fail("invalid native caller");
    caller = stack;
    entry = storage + twine::render::visibility_stack_size - headroom;
    for (uint32_t i = 0; i < 16; i += 4) {
        TWINE_MEM_W(i, storage) = guard;
        TWINE_MEM_W(twine::render::visibility_stack_size - 16 + i, storage) = guard;
    }

    std::memcpy(rdram + entry - kseg0 + 0x20, rdram + caller - kseg0 + 0x20, 16);
    ctx->r29 = twine_n64_address(entry);
}
extern "C" void twine_check_visibility_stack(uint8_t*, recomp_context* ctx) {

    const uint32_t stack = uint32_t(ctx->r29);
    if (!caller || stack > entry || stack < storage + 0x1000 + 0x110)
        fail("native walk exceeded its bounded stack");
}
extern "C" void twine_end_visibility_stack(uint8_t* rdram, recomp_context* ctx) {
    if (!caller || uint32_t(ctx->r29) != entry) fail("unbalanced native return");
    for (uint32_t i = 0; i < 16; i += 4) {
        if (uint32_t(TWINE_MEM_W(i, storage)) != guard ||
                uint32_t(TWINE_MEM_W(twine::render::visibility_stack_size - 16 + i, storage)) != guard)
            fail("native traversal crossed its storage boundary");
    }
    ctx->r29 = twine_n64_address(caller);
    caller = entry = 0;
}
