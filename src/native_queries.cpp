#include "native_queries.hpp"

#include <bit>
#include <cmath>
#include "funcs.h"
#include "twine_recomp.h"
#include "world_render_context.hpp"

namespace {
bool valid(uint32_t address, uint32_t size) {
    return address >= 0x80000000U && address < 0x80800000U &&
        size <= 0x80800000U - address;
}
float read_float(uint8_t* rdram, uint32_t address, uint32_t offset) {
    return std::bit_cast<float>(uint32_t(TWINE_MEM_W(offset, address)));
}

enum class Kind { Camera, Text, Objectives };
struct Query {
    const recomp_context* context;
    Kind kind;
    twine::render::NativeCamera* camera = nullptr;
    bool captured = false;
    std::span<twine::native::Objective> objectives;
    uint32_t table = 0;
    size_t count = 0;
    bool invalid = false;
};
thread_local Query* active = nullptr;
class Scope {
    Query* previous_ = active;
public:
    explicit Scope(Query& query) { active = &query; }
    ~Scope() { active = previous_; }
};
bool querying(const recomp_context* ctx, Kind kind) {
    return active && active->context == ctx && active->kind == kind;
}
}

twine::native::Queries::Queries(uint8_t* rdram, const recomp_context* caller)
    : rdram_(rdram) {
    if (!rdram || !caller) { return; }
    const uint32_t stack = uint32_t(caller->r29);

    if ((stack & 7U) || stack < 0x80000400U || !valid(stack - 0x400U, 0x400U)) { return; }
    stack_ = stack - 0x40U;
    context_.mips3_float_mode = caller->mips3_float_mode;
    context_.status_reg = caller->status_reg;
    context_.f_odd = context_.mips3_float_mode ? &context_.f1.u32l : &context_.f0.u32h;
}

bool twine::native::Queries::camera(uint32_t player, render::NativeCamera& result) {
    uint8_t* rdram = rdram_;
    result = {};
    if (!stack_ || (player & 3U) || !valid(player, 0x70)) { return false; }
    const uint32_t inventory = TWINE_MEM_W(0x6C, player);
    if ((inventory & 3U) || !valid(inventory, 0x185)) { return false; }
    const uint8_t index = TWINE_MEM_BU(0x182, inventory);
    if (index >= 4) { return false; }
    const uint32_t camera = TWINE_MEM_W(index * 4, 0x80109688U);
    if ((camera & 3U) || !valid(camera, 0xF8)) { return false; }

    for (uint32_t offset : {0x10U, 0x14U, 0x18U}) {
        if (!std::isfinite(read_float(rdram_, camera, offset))) { return false; }
    }
    for (uint32_t offset : {0x34U, 0x38U, 0x78U, 0x7CU}) {
        const float value = read_float(rdram_, camera, offset);
        if (!std::isfinite(value) || std::abs(value) > 1000000.0f) { return false; }
    }
    for (uint32_t offset : {0x4CU, 0x68U, 0x6CU}) {
        const float value = read_float(rdram_, camera, offset);
        if (!std::isfinite(value) || value < 0.000001f || value > 1000000.0f) { return false; }
    }
    for (uint32_t offset : {0x24U, 0x28U}) {
        const float value = read_float(rdram_, inventory, offset);
        if (!std::isfinite(value) || std::abs(value) > 1000000.0f) { return false; }
    }
    context_.r29 = twine_n64_address(stack_);
    context_.r4 = twine_n64_address(player);
    Query query{&context_, Kind::Camera, &result};
    Scope scope(query);
    func_8006832C(rdram_, &context_);
    if (!query.captured) { result = {}; return false; }
    result.room = TWINE_MEM_W(0xF4, camera);
    return true;
}

bool twine::native::Queries::ammo(uint32_t inventory, uint8_t item) {
    uint8_t* rdram = rdram_;
    if (!stack_ || (inventory & 3U) || !valid(inventory, 0x410) || item >= 59) { return false; }
    const uint32_t record = 0x800C469CU + uint32_t(item) * 232U;

    if (TWINE_MEM_BU(0x80, record) >= 32 ||
        (TWINE_MEM_B(4, record) != 0 && TWINE_MEM_HU(2, record) >= 59)) { return false; }
    context_.r29 = twine_n64_address(stack_);
    context_.r4 = twine_n64_address(inventory);
    context_.r5 = item;
    func_80063D38(rdram_, &context_);
    return context_.r2 != 0;
}

bool twine::native::Queries::equipment(uint32_t inventory, uint8_t id) {
    if (!stack_ || (inventory & 1U) || !valid(inventory, 0x174)) { return false; }
    context_.r29 = twine_n64_address(stack_);
    context_.r4 = twine_n64_address(inventory);
    context_.r5 = id;
    context_.r6 = 1;
    func_800707B4(rdram_, &context_);
    return context_.r2 != 0;
}

uint32_t twine::native::Queries::text(uint16_t resource) {
    if (!stack_) { return 0; }
    context_.r29 = twine_n64_address(stack_);
    context_.r4 = resource;
    Query query{&context_, Kind::Text};
    Scope scope(query);
    func_80087100(rdram_, &context_);
    const uint32_t result = uint32_t(context_.r2);
    return valid(result, 1) ? result : 0;
}

bool twine::native::Queries::objectives(std::span<Objective> rows, size_t& count) {
    count = 0;
    if (!stack_ || rows.size() > 64) { return false; }
    auto* rdram = rdram_;
    const uint32_t table = TWINE_MEM_W(0, 0x800BFF04U);
    const uint16_t entries = TWINE_MEM_HU(0, 0x800E1608U);
    if (entries > rows.size() || (table & 3U) ||
            (entries && !valid(table, uint32_t(entries) * 8))) { return false; }
    if (!entries) { return true; }
    context_.r29 = twine_n64_address(stack_);

    context_.r3 = 1;
    context_.r4 = 0;
    context_.r5 = 1;
    Query query{&context_, Kind::Objectives};
    query.objectives = rows.first(entries);
    query.table = table;
    Scope scope(query);
    twine_native_objectives(rdram_, &context_);
    if (query.invalid || context_.r2 != query.count) { return false; }
    count = query.count;
    return true;
}

extern "C" void twine_capture_objective(uint8_t* rdram, recomp_context* ctx) {
    if (!querying(ctx, Kind::Objectives)) { return; }

    const auto index = uint32_t(ctx->r5);
    if (index >= active->objectives.size() || active->count >= active->objectives.size()) {
        active->invalid = true;
        return;
    }
    const auto entry = active->table + index * 8;
    active->objectives[active->count++] = {TWINE_MEM_H(0, entry),
        TWINE_MEM_HU(4, entry), uint8_t(TWINE_MEM_HU(2, entry) & 0xFU)};
}

extern "C" uint32_t twine_camera_query_active(recomp_context* ctx) {
    return querying(ctx, Kind::Camera);
}

extern "C" uint32_t twine_capture_camera_query(uint8_t* rdram, recomp_context* ctx) {
    if (!querying(ctx, Kind::Camera)) { return 0; }

    auto& camera = *active->camera;
    const uint32_t stack = uint32_t(ctx->r29);
    bool finite = true;
    for (uint32_t i = 0; i < 3; ++i) {
        camera.position[i] = read_float(rdram, stack, 0x48U + i * 4U);
        camera.aim[i] = read_float(rdram, stack, 0x58U + i * 4U);
        camera.forward[i] = read_float(rdram, stack, 0x90U + i * 12U);
        finite &= std::isfinite(camera.position[i]) && std::isfinite(camera.aim[i]) &&
            std::isfinite(camera.forward[i]);
    }
    active->captured = finite && (camera.aim[0] != 0 || camera.aim[1] != 0 || camera.aim[2] != 0);
    return 1;
}

extern "C" uint32_t twine_text_query_active(recomp_context* ctx) {
    return querying(ctx, Kind::Text);
}

extern "C" uint32_t twine_query_invalid_word(uint32_t address) {
    return (address & 3U) || !valid(address, 4);
}

extern "C" uint32_t twine_text_query_invalid_span(recomp_context* ctx) {
    const uint32_t base = uint32_t(ctx->r3), offset = uint32_t(ctx->r2);
    return querying(ctx, Kind::Text) &&
        (!valid(base, 1) || offset >= 0x80800000U - base);
}
