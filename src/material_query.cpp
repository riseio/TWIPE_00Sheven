#include "material_query.hpp"
#include "twine_recomp.h"
#include <stdexcept>
#include <exception>

extern "C" void twine_native_materials(uint8_t*, recomp_context*);

namespace {
constexpr uint32_t stream = 0x80200000U, images_address = 0x80501000U,
    palettes_address = 0x80500000U, counters = 0x80504000U, stack = 0x807FF000U;
struct Query {
    const recomp_context* context;
    uint32_t size, image_count, palette_count;
    twine::textures::MaterialPairs& pairs;
    uint32_t cursor = stream - 1;
    unsigned texture = UINT32_MAX, palette = UINT32_MAX;
    const char* failure = nullptr;
    std::exception_ptr exception;
};
thread_local Query* active = nullptr;
class Scope {
    Query* previous_ = active;
public:
    explicit Scope(Query& query) { active = &query; }
    ~Scope() { active = previous_; }
};
Query* query(const recomp_context* ctx) {
    return active && active->context == ctx ? active : nullptr;
}
}

void twine::textures::collect_material_pairs(std::span<uint8_t> arena,
        std::span<const uint8_t> commands, std::span<const uint8_t> images,
        std::span<const uint8_t> palettes, MaterialPairs& result) {
    if (arena.size() != 8 * 1024 * 1024 || commands.empty() || commands.size() > 2 * 1024 * 1024 ||
            images.size() % 12 || images.size() > 256 * 12 ||
            palettes.size() % 8 || palettes.size() > 256 * 8) {
        throw std::runtime_error("Invalid native material query extent");
    }
    auto* rdram = arena.data();
    const auto copy = [&](uint32_t address, std::span<const uint8_t> bytes) {
        for (size_t i = 0; i < bytes.size(); ++i) { TWINE_MEM_B(i, address) = bytes[i]; }
    };
    copy(stream, commands);
    copy(images_address, images);
    copy(palettes_address, palettes);
    TWINE_MEM_W(0, counters) = TWINE_MEM_W(4, counters) = 0;

    recomp_context context{};
    context.r29 = twine_n64_address(stack - 0x90U);
    context.r4 = twine_n64_address(stream);
    TWINE_MEM_W(16, stack) = 1;
    TWINE_MEM_W(20, stack) = 0;
    TWINE_MEM_W(24, stack) = images_address;
    TWINE_MEM_W(28, stack) = palettes_address;
    TWINE_MEM_W(32, stack) = 0;
    TWINE_MEM_W(36, stack) = counters;
    TWINE_MEM_W(40, stack) = counters + 4;
    Query state{&context, uint32_t(commands.size()), uint32_t(images.size() / 12),
        uint32_t(palettes.size() / 8), result};
    Scope scope(state);
    twine_native_materials(rdram, &context);
    if (state.exception) { std::rethrow_exception(state.exception); }
    if (state.failure) { throw std::runtime_error(state.failure); }
}

extern "C" uint32_t twine_material_require(recomp_context* ctx, uint32_t address, uint32_t size) {
    auto* state = query(ctx);
    if (!state) { return 0; }
    if (address < stream || size > state->size || address - stream > state->size - size) {
        state->failure = "Native material command reads outside its stream";
        return 0;
    }
    return 1;
}

extern "C" uint32_t twine_material_cursor(recomp_context* ctx) {
    const auto address = uint32_t(ctx->r17);
    if (!twine_material_require(ctx, address, 1)) { return 0; }
    auto& state = *query(ctx);
    if (address <= state.cursor) {
        state.failure = "Native material parser did not advance";
        return 0;
    }
    state.cursor = address;
    return 1;
}

extern "C" uint32_t twine_material_select(uint8_t* rdram, recomp_context* ctx, uint32_t palette) {
    const auto command = uint32_t(ctx->r4);
    if (!twine_material_require(ctx, command, 2)) { return 0; }
    auto& state = *query(ctx);
    const unsigned index = TWINE_MEM_BU(1, command);
    if (index >= (palette ? state.palette_count : state.image_count)) {
        state.failure = "Native material selection exceeds its bank";
        return 0;
    }
    (palette ? state.palette : state.texture) = index;
    return 1;
}

extern "C" uint32_t twine_material_draw(recomp_context* ctx) {
    auto* state = query(ctx);
    if (!state) { return 0; }
    try {
        if (state->texture != UINT32_MAX) { state->pairs[state->texture].insert(state->palette); }
    } catch (...) {

        state->exception = std::current_exception();
        return 0;
    }
    return 1;
}
