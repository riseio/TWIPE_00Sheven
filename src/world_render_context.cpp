#include "world_render_context.hpp"

#include "native_queries.hpp"
#include "twine_recomp.h"
#include "librecomp/addresses.hpp"
#include "shared/rt64_modern_grapple.h"

namespace {
bool valid_range(uint32_t address, uint32_t size) {
    return address >= 0x80000000U && address < 0x80800000U &&
        size <= 0x80800000U - address;
}

}

extern "C" void twine_capture_render_task(uint8_t* rdram, recomp_context* ctx) {

    const uint32_t task = static_cast<uint32_t>(ctx->r17);
    if (!rdram || !valid_range(task, 0x40)) { return; }
    const uint32_t displayList = TWINE_MEM_W(0x30, task);
    const uint32_t size = TWINE_MEM_W(0x34, task);
    const uint32_t physical = displayList & 0x1FFFFFFFU;
    const bool valid = physical != 0 && physical < recomp::mem_size && size != 0 &&
        size <= recomp::mem_size - physical && (size & 7U) == 0;
    auto state = RT64::getModernRenderState();

    state.grapple.visible = state.grapple.visible && state.worldActive;
    state.worldMatrixOwnershipKnown = true;
    state.worldMatrixAddress = 0;

    const uint32_t frame = TWINE_MEM_W(0, 0x80117728U);
    if (valid_range(frame, 0x750)) {
        const uint32_t worldMatrix = TWINE_MEM_W(0x74C, frame);
        if (valid_range(worldMatrix, 64) && (worldMatrix & 7U) == 0) {
            state.worldMatrixAddress = twine_render_matrix_address(task, worldMatrix) & 0x1FFFFFFFU;
        }
    }
    if (valid) {
        RT64::captureModernRenderTask(displayList, state);
    }
}

namespace twine::render {
bool read_native_camera(uint8_t* rdram, recomp_context* ctx, uint32_t player, NativeCamera& camera) {
    return twine::native::Queries(rdram, ctx).camera(player, camera);
}

void publish_native_camera(uint8_t* rdram, recomp_context* ctx, uint32_t player) {
    if (!rdram || !valid_range(player, 0x70)) { return; }
    const uint32_t inventory = TWINE_MEM_W(0x6C, player);
    if (!valid_range(inventory, 0x183) || TWINE_MEM_BU(0x182, inventory) != 0) { return; }
    NativeCamera native{};
    RT64::ModernCameraState published{};
    published.valid = read_native_camera(rdram, ctx, player, native);
    published.position = {native.position[0], native.position[1], native.position[2]};
    published.forward = {native.forward[0], native.forward[1], native.forward[2]};
    RT64::setModernCameraState(published);
}
}
