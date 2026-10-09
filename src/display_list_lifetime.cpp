#include "twine_recomp.h"
#include "ultramodern/extensions.h"

#include <cstdio>
#include <cstdlib>

namespace {

constexpr uint32_t frame_size = 32;
static_assert(sizeof(OSMesgQueue) + sizeof(OSMesg) <= frame_size);

[[noreturn]] void invalid(const char* reason) {
    std::fprintf(stderr, "Display-list lifetime failed: %s\n", reason);
    std::abort();
}
bool native_span(uint32_t address, uint32_t size) {
    return !(address & 7U) && address >= 0x80000000U &&
        address <= 0x80800000U - size;
}
}

extern "C" void twine_begin_display_list_read(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t task = uint32_t(ctx->r17);
    const uint32_t stack = uint32_t(ctx->r29);
    if (!native_span(task, sizeof(OSTask)) || stack < frame_size ||
            !native_span(stack - frame_size, frame_size)) invalid("invalid task or stack");
    const int32_t queue = int32_t(stack - frame_size);
    const int32_t list = TWINE_MEM_W(0x30, task);
    ctx->r29 = twine_n64_address(uint32_t(queue));
    osCreateMesgQueue(rdram, queue, queue + sizeof(OSMesgQueue), 1);

    osExQueueDisplaylistEvent(queue, 0, list, OS_EX_DISPLAYLIST_EVENT_PARSED);
}

extern "C" void twine_finish_display_list_read(uint8_t* rdram, recomp_context* ctx) {
    const int32_t queue = int32_t(ctx->r29);

    if (osRecvMesg(rdram, queue, 0, OS_MESG_BLOCK) != 0) invalid("parse completion was not received");
    ctx->r29 = twine_n64_address(uint32_t(queue) + frame_size);

}
