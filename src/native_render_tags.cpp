#include "native_render_tags.hpp"
#include "save_state_game.hpp"
#include "save_state_codec.hpp"
#include "save_state_owner.hpp"
#include "shared/rt64_modern_grapple.h"
#include <cmath>
#include "librecomp/state.hpp"
#include "native_render_matrix.hpp"
#include "native_camera_history.hpp"
#include "native_visibility.hpp"
#include "visibility_stack.hpp"
#include "native_render_commands.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "librecomp/addresses.hpp"
#include "../lib/rt64/include/rt64_extended_gbi.h"
#include "twine_recomp.h"

namespace {
constexpr uint32_t kseg0 = 0x80000000U;
constexpr uint32_t hook = 0xE0525464U;
constexpr uint32_t extended = 0x64000000U;
constexpr uint32_t matrix_group = extended | G_EX_MATRIXGROUP_V1;
constexpr uint32_t arena_size = 1024 * 1024;
constexpr size_t task_count = 8;
using twine::render::Command;
struct MatrixEntry { uint32_t native = 0, snapshot = 0, fixed_copy = 0, rendered = 0; };
struct TaskArena {
    uint32_t task = 0;
    uint32_t base = 0;
    uint32_t used = 0;
    std::array<uint32_t, 6> cameras{};
    std::array<bool, 6> camera_cuts{};
    std::array<MatrixEntry, 4096> matrices{};
};
std::array<TaskArena, task_count> arenas;
twine::render::TransformIds identities;
twine::render::CameraHistory camera_history;
uint64_t render_frame = 0;
TaskArena* active = nullptr;
uint32_t scoped_owner = 0;
uint32_t scoped_command = 0;
uint32_t sprite_owner = 0;
uint32_t deferred_projection = 0;
struct StaticDraw { uint32_t owner = 0, part = 0, begin = 0; };
StaticDraw static_draw;

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "Native render tagging failed: %s\n", message);

    std::abort();
}

bool native_range(uint32_t address, uint32_t size) {
    return address >= kseg0 && address < kseg0 + 0x800000U &&
        size <= kseg0 + 0x800000U - address;
}

uint32_t reserve(uint32_t bytes) {
    if (!active || bytes > arena_size - active->used) {
        throw std::runtime_error("Native render command arena exhausted or missing task owner");
    }
    const uint32_t address = active->base + active->used;
    active->used += bytes;
    return address;
}

template<size_t N>
uint32_t store(uint8_t* rdram, const std::array<Command, N>& commands) {
    const uint32_t address = reserve(sizeof(commands));
    std::memcpy(rdram + (address - kseg0), commands.data(), sizeof(commands));
    return address;
}

uint32_t store_matrix(uint8_t* rdram, const twine::render::Matrix& matrix) {
    twine::render::validate_matrix(matrix);
    const uint32_t address = reserve(sizeof(matrix));
    std::memcpy(rdram + (address - kseg0), matrix.data(), sizeof(matrix));
    return address;
}

MatrixEntry& matrix_entry(uint32_t address) {
    if (!active) { throw std::runtime_error("Missing native matrix task"); }
    address = (address & 0x1FFFFFFFU) | kseg0;
    const size_t start = (address >> 6) % active->matrices.size();
    for (size_t i = 0; i < active->matrices.size(); ++i) {
        auto& entry = active->matrices[(start + i) % active->matrices.size()];
        if (entry.native == 0 || entry.native == address) { return entry; }
    }
    throw std::runtime_error("Native matrix snapshot capacity exceeded");
}

twine::render::Matrix read_matrix(uint8_t* rdram, uint32_t address) {
    address = (address & 0x1FFFFFFFU) | kseg0;
    if (!native_range(address, 64)) { throw std::runtime_error("Invalid native matrix address"); }
    twine::render::Matrix matrix{};
    const auto& entry = matrix_entry(address);
    if (entry.native == address && entry.fixed_copy && std::memcmp(
            rdram + (address - kseg0), rdram + (entry.fixed_copy - kseg0), 64) == 0) {
        std::memcpy(matrix.data(), rdram + (entry.snapshot - kseg0), sizeof(matrix));
    }
    else {

        for (uint32_t i = 0; i < 16; ++i) {
            const int32_t integer = TWINE_MEM_H(i * 2, address);
            const uint32_t fraction = TWINE_MEM_HU(32 + i * 2, address);
            matrix[i] = float(integer) + float(fraction) / 65536.0f;
        }
    }
    twine::render::validate_matrix(matrix);
    return matrix;
}

void tag(uint8_t* rdram, uint32_t command, uint32_t owner,
        uint32_t resource, uint32_t part, uint32_t domain) {
    if (!native_range(command, sizeof(Command)) || (command & 7U)) {
        throw std::runtime_error("Invalid native matrix command address");
    }
    const Command original{uint32_t(TWINE_MEM_W(0, command)),
        uint32_t(TWINE_MEM_W(4, command))};
    if ((original.w0 & 0xFFFF0000U) != 0xDA380000U) {
        throw std::runtime_error("Native render hook does not own a matrix command");
    }
    const bool projection = (original.w0 & 4U) != 0;
    const uint32_t id = domain == 0 ? G_EX_ID_IGNORE :
        identities.get({owner, resource, part, domain});

    const uint32_t view = TWINE_MEM_W(0, 0x800E1710U);
    const bool camera_cut = active && view < active->camera_cuts.size() && active->camera_cuts[view];

    const bool camera_space = domain == 7 || domain == 8 ||
        (domain == 9 && TWINE_MEM_W(0, 0x80112E40U) == 2);
    const bool interpolate_model = domain != 0 && !(camera_cut && camera_space);
    const bool interpolate_rotation = !(camera_cut && domain >= 6);
    const uint32_t flags = (projection ? 2U : 0U) | (!interpolate_model ? 0U :
        ((!projection && domain != 3) ? 4U : 0U) |
        (1U << 3) | (uint32_t(interpolate_rotation) << 5) | (1U << 7) | (1U << 9) |
        (1U << 11) | (1U << 15) | (2U << 24) |

        (domain == 4 ? (1U << 22) : 0U));
    auto& snapshot = matrix_entry(original.w1);
    const uint32_t matrix_address = (original.w1 & 0x1FFFFFFFU) | kseg0;
    const bool float_valid = snapshot.native && snapshot.fixed_copy && native_range(matrix_address, 64) &&
        std::memcmp(rdram + (matrix_address - kseg0), rdram + (snapshot.fixed_copy - kseg0), 64) == 0;
    const uint32_t wrapper = float_valid ? store(rdram, std::array<Command, 6>{{
        {hook, 0x10000064U}, {matrix_group, id}, {flags, 0},
        {extended | G_EX_MATRIX_FLOAT_V1, original.w0 & 0xFFU},
        {0, snapshot.snapshot}, {0xDF000000U, 0}
    }}) : store(rdram, std::array<Command, 5>{{
        {hook, 0x10000064U}, {matrix_group, id}, {flags, 0},
        original, {0xDF000000U, 0}
    }});
    snapshot.native = matrix_address;
    snapshot.rendered = float_valid ? snapshot.snapshot : matrix_address;

    TWINE_MEM_W(0, command) = 0xDE000000U;
    TWINE_MEM_W(4, command) = wrapper;
}
}

uint32_t twine::render::ui_identity(uint32_t node, uint32_t root, uint32_t resource) {
    return identities.get({node, root, resource, 10});
}

void twine::render::prefix_command(uint8_t* rdram, uint32_t address,
        std::span<const Command> prefix) {
    if (!native_range(address, 8) || (address & 7U) || prefix.size() > 32) {
        throw std::runtime_error("Invalid native display-list command wrapper");
    }
    const uint32_t wrapper = reserve(uint32_t((prefix.size() + 2) * sizeof(Command)));
    auto* output = rdram + (wrapper - kseg0);
    std::memcpy(output, prefix.data(), prefix.size_bytes());
    std::memcpy(output + prefix.size_bytes(), rdram + (address - kseg0), 8);
    const Command end{0xDF000000U, 0};
    std::memcpy(output + prefix.size_bytes() + 8, &end, sizeof(end));
    TWINE_MEM_W(0, address) = 0xDE000000U;
    TWINE_MEM_W(4, address) = wrapper;
}

void twine::render::wrap_commands(uint8_t* rdram, uint32_t begin, uint32_t end,
        std::span<const Command> prefix, std::span<const Command> suffix) {
    if (end < begin || (begin & 7U) || (end & 7U) ||
            !native_range(begin, end - begin) || prefix.size() > 32 || suffix.size() > 32) {
        throw std::runtime_error("Invalid native display-list scope");
    }
    if (begin == end) { return; }
    const uint32_t size = end - begin;
    const uint32_t wrapper = reserve(uint32_t(prefix.size_bytes() + size + suffix.size_bytes() + 8));
    auto* output = rdram + (wrapper - kseg0);
    std::memcpy(output, prefix.data(), prefix.size_bytes());
    output += prefix.size_bytes();
    std::memcpy(output, rdram + (begin - kseg0), size);
    output += size;
    std::memcpy(output, suffix.data(), suffix.size_bytes());
    output += suffix.size_bytes();

    const Command resume{0xDE010000U, end};
    std::memcpy(output, &resume, sizeof(resume));
    TWINE_MEM_W(0, begin) = 0xDE010000U;
    TWINE_MEM_W(4, begin) = wrapper;
}

extern "C" void twine_init_render_tags(uint8_t* rdram, recomp_context*) try {
    arenas = {};
    active = nullptr;
    identities.cut();
    camera_history.reset();
    render_frame = 0;
    twine::render::reset_visibility();
    scoped_owner = 0;
    sprite_owner = 0;
    auto* memory = static_cast<uint8_t*>(recomp::alloc(rdram, task_count * arena_size));
    if (!memory || memory < rdram ||
            uint64_t(memory - rdram) > recomp::mem_size - task_count * arena_size) {
        throw std::runtime_error("Cannot allocate native render command arenas");
    }
    for (size_t i = 0; i < arenas.size(); ++i) {
        arenas[i].base = kseg0 + uint32_t(memory - rdram) + uint32_t(i) * arena_size;
    }
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_begin_render_tags(uint8_t* rdram, recomp_context* ctx) try {
    const uint32_t task = uint32_t(ctx->r20);
    if (!native_range(task, 0x798)) { throw std::runtime_error("Invalid native render task"); }
    const uint32_t index = TWINE_MEM_HU(0, 0x8010A4B0U);
    if (index >= arenas.size()) { throw std::runtime_error("Native render task index out of range"); }
    active = &arenas[index];
    if (!active->base) { throw std::runtime_error("Native render arenas are not initialized"); }
    active->task = task;
    active->used = 0;
    active->cameras = {};
    active->camera_cuts = {};
    active->matrices = {};
    deferred_projection = 0;
    static_draw = {};
    sprite_owner = 0;
    identities.begin_frame();
    if (++render_frame == 0) { throw std::runtime_error("Native camera frame overflow"); }
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_finish_render_tags(uint8_t* rdram, recomp_context* ctx) try {
    const uint32_t task = uint32_t(ctx->r17);
    if (!active || active->task != task) { throw std::runtime_error("Native render task ownership changed"); }
    const uint32_t native_dl = TWINE_MEM_W(0x30, task);
    const uint32_t root = store(rdram, std::array<Command, 10>{{
        {hook, 0x10000064U}, {extended | G_EX_SETRDRAMEXTENDED_V1, 1},
        {extended | G_EX_SETREFRESHRATE_V1, 30},

        {extended | G_EX_SETNEARCLIPPING_V1, 0},
        {extended | G_EX_SETTEXCOORDWRAPPOINT_V1, (1024U << 16) | 1024U},
        {matrix_group, 0}, {0, 0},
        {matrix_group, 0}, {2, 0},
        {0xDE010000U, native_dl}
    }});
    TWINE_MEM_W(0x30, task) = root;
    TWINE_MEM_W(0x34, task) = 10 * sizeof(Command);
    active = nullptr;
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_tag_model(uint8_t* rdram, uint32_t command,
        uint32_t owner, uint32_t resource, uint32_t part, uint32_t domain) try {
    tag(rdram, command, owner, resource, part, domain);
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_begin_static_draw(uint8_t* rdram, uint32_t owner, uint32_t part) try {
    if (!active || !native_range(owner, 1) || static_draw.owner) {
        throw std::runtime_error("Invalid or nested native static draw owner");
    }
    static_draw = {owner, part, uint32_t(TWINE_MEM_W(0, 0x80115328U))};
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_finish_static_draw(uint8_t* rdram, uint32_t resource) try {
    if (!static_draw.owner) { return; }
    const auto draw = static_draw;
    static_draw = {};
    const uint32_t end = uint32_t(TWINE_MEM_W(0, 0x80115328U));

    if (end == draw.begin) { return; }
    if (!native_range(draw.begin, 8) || (draw.begin & 7U) ||
            !native_range(resource, 0x48) || end != draw.begin + 8 ||
            uint32_t(TWINE_MEM_W(0, draw.begin)) != 0xDE000000U) {
        throw std::runtime_error("Native static model writer changed its command extent");
    }
    const uint32_t id = identities.get({draw.owner, resource, draw.part, 2});
    const uint32_t view = TWINE_MEM_W(0, 0x800E1710U);
    if (!active || view >= active->camera_cuts.size()) { throw std::runtime_error("Static draw has no camera view"); }
    const bool sky_cut = draw.part >= 2 && active->camera_cuts[view];
    const uint32_t flags = G_EX_PUSH | (sky_cut ? 0U : 4U | (1U << 3) | (1U << 5) |
        (1U << 7) | (1U << 9) | (1U << 11) | (1U << 13) |
        (1U << 15) | (1U << 22) | (2U << 24));
    const uint32_t frame = uint32_t(TWINE_MEM_W(0, 0x80117728U));
    if (!native_range(frame, 0x750)) { throw std::runtime_error("Static draw has no world matrix frame"); }
    const uint32_t world = uint32_t(TWINE_MEM_W(0x74C, frame));
    if (!native_range(world, 64)) { throw std::runtime_error("Static draw has no world matrix owner"); }
    const uint32_t matrix = twine_render_matrix_address(active->task, world);
    const bool float_matrix = matrix != world;
    const Command original{uint32_t(TWINE_MEM_W(0, draw.begin)),
        uint32_t(TWINE_MEM_W(4, draw.begin))};

    const uint32_t wrapper = store(rdram, std::array<Command, 9>{{
        {hook, 0x10000064U}, {matrix_group, id}, {flags, 0},
        float_matrix ? Command{extended | G_EX_MATRIX_FLOAT_V1, 2} : Command{0xDA380002U, matrix},
        float_matrix ? Command{0, matrix} : Command{0, 0},
        original, {0xD8380002U, 64},
        {extended | G_EX_POPMATRIXGROUP_V1, 1}, {0xDF000000U, 0}
    }});
    TWINE_MEM_W(0, draw.begin) = 0xDE000000U;
    TWINE_MEM_W(4, draw.begin) = wrapper;
} catch (const std::exception& error) { fail(error.what()); }

extern "C" uint32_t twine_render_matrix_address(uint32_t task, uint32_t native) {
    native = (native & 0x1FFFFFFFU) | kseg0;

    for (const auto& arena : arenas) {
        if (arena.task != task) { continue; }
        const size_t start = (native >> 6) % arena.matrices.size();
        for (size_t i = 0; i < arena.matrices.size(); ++i) {
            const auto& entry = arena.matrices[(start + i) % arena.matrices.size()];
            if (entry.native == 0) { break; }
            if (entry.native == native) { return entry.rendered ? entry.rendered : native; }
        }
        break;
    }
    return native;
}

extern "C" void twine_tag_projection(uint8_t* rdram, uint32_t command, uint32_t role) try {
    const uint32_t view = TWINE_MEM_W(0, 0x800E1710U);
    if (!active || view >= active->cameras.size()) { throw std::runtime_error("Invalid native view index"); }
    const uint32_t camera = active->cameras[view];

    const uint32_t cursor = uint32_t(TWINE_MEM_W(0, 0x80115328U));
    if (role == 4 && cursor != command + 8 && cursor != command + 32) {
        throw std::runtime_error("Invalid native role-4 projection extent");
    }
    const bool has_view = role != 4 || cursor == command + 32;
    if (!native_range(command, has_view ? 16 : 8) || (command & 7U)) {
        throw std::runtime_error("Invalid native projection command range");
    }
    const Command lens{uint32_t(TWINE_MEM_W(0, command)), uint32_t(TWINE_MEM_W(4, command))};
    if (lens.w0 != 0xDA380007U) { throw std::runtime_error("Missing native projection load"); }
    auto projection = read_matrix(rdram, lens.w1);
    auto view_matrix = twine::render::identity_matrix();
    if (has_view) {
        const Command view_command{uint32_t(TWINE_MEM_W(8, command)), uint32_t(TWINE_MEM_W(12, command))};
        if (view_command.w0 != 0xDA380005U) { throw std::runtime_error("Missing native camera multiply"); }
        view_matrix = read_matrix(rdram, view_command.w1);
        twine::render::separate_camera_scale(projection, view_matrix);
    }
    auto view_correction = twine::render::identity_matrix();
    if ((role == 2 || (role == 4 && has_view)) && camera) {

        const uint32_t frame = uint32_t(TWINE_MEM_W(0, 0x80117728U));
        if (!native_range(frame, 0x300)) {
            throw std::runtime_error("Missing native camera matrix owner");
        }
        auto world_view = read_matrix(rdram, frame + 0x180 + view * 64);
        auto unused_lens = twine::render::identity_matrix();
        twine::render::separate_camera_scale(unused_lens, world_view);
        if (role == 4) {

            world_view[12] = world_view[13] = world_view[14] = 0;
        }
        view_correction = twine::render::multiply_matrix(view_matrix,
            twine::render::inverse_affine_matrix(world_view));
    }
    const uint32_t projection_address = store_matrix(rdram, projection);
    const uint32_t view_address = store_matrix(rdram, view_matrix);
    const uint32_t correction_address = store_matrix(rdram, view_correction);
    const uint32_t id = camera ? identities.get({camera, 0, role, 1}) : G_EX_ID_IGNORE;
    const uint32_t flags = 2U | (G_EX_ASPECT_ADJUST << 20) | (camera && !active->camera_cuts[view] ?
        (1U << 3) | (1U << 5) | (1U << 7) | (1U << 9) |
        (1U << 11) | (1U << 15) | (2U << 24) : 0U);

    const uint32_t wrapper = store(rdram, std::array<Command, 9>{{
        {hook, 0x10000064U}, {extended | G_EX_SETVIEWMATRIXFLOAT_V1, correction_address},
        {matrix_group, id}, {flags, 0},
        {extended | G_EX_MATRIX_FLOAT_V1, 7}, {0, projection_address},
        {extended | G_EX_MATRIX_FLOAT_V1, 5}, {0, view_address},
        {0xDF000000U, 0}
    }});
    TWINE_MEM_W(0, command) = 0xDE000000U;
    TWINE_MEM_W(4, command) = wrapper;
    if (has_view) {
        TWINE_MEM_W(8, command) = 0;
        TWINE_MEM_W(12, command) = 0;
    }
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_capture_float_matrix(uint8_t* rdram, recomp_context* ctx) try {
    if (!active) { return; }
    const uint32_t source = uint32_t(ctx->r4), destination = uint32_t(ctx->r5);
    if (!native_range(source, 64) || !native_range(destination, 64)) {
        throw std::runtime_error("Invalid native matrix conversion range");
    }
    twine::render::Matrix matrix;
    std::memcpy(matrix.data(), rdram + (source - kseg0), sizeof(matrix));
    auto& entry = matrix_entry(destination);
    entry = {destination, store_matrix(rdram, matrix), 0, entry.rendered};
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_finish_float_matrix(uint8_t* rdram, recomp_context* ctx) try {
    if (!active) { return; }

    const uint32_t destination = uint32_t(ctx->r5) - 32;
    if (!native_range(destination, 64)) { throw std::runtime_error("Invalid completed native matrix"); }
    auto& entry = matrix_entry(destination);
    if (entry.native != destination) { throw std::runtime_error("Native matrix conversion lost its owner"); }
    entry.fixed_copy = reserve(64);
    std::memcpy(rdram + (entry.fixed_copy - kseg0), rdram + (destination - kseg0), 64);
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_defer_projection_tag(uint32_t command) { deferred_projection = command; }
extern "C" void twine_finish_projection_tag(uint8_t* rdram) {
    twine_tag_projection(rdram, deferred_projection, 4);
    deferred_projection = 0;
}

extern "C" void twine_render_camera(uint8_t* rdram, uint32_t camera, uint32_t view) try {
    if (!active || view >= active->cameras.size() || !native_range(camera, 0x14C)) {
        throw std::runtime_error("Invalid native render camera");
    }
    active->cameras[view] = camera;
    std::array<float, 3> position;

    std::memcpy(position.data(), rdram + (camera - kseg0) + 0x1C, sizeof(position));
    active->camera_cuts[view] = camera_history.update(render_frame, camera,
        uint32_t(TWINE_MEM_W(0x138, camera)), position);
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_prepare_visibility(uint8_t* rdram, uint32_t camera, uint32_t view) try {
    if (!active || view >= active->cameras.size() || active->cameras[view] != camera) {
        throw std::runtime_error("Native visibility lost its render camera");
    }
    twine::render::prepare_visibility_stack(reserve(twine::render::visibility_stack_size));
    const uint32_t frame=uint32_t(TWINE_MEM_W(0,0x80117728U));
    if (!native_range(frame,0x360)) { throw std::runtime_error("Invalid visibility matrix frame"); }
    auto lens=read_matrix(rdram,frame+view*64);
    auto cameraView=read_matrix(rdram,frame+0x180+view*64);
    twine::render::separate_camera_scale(lens,cameraView);
    active->camera_cuts[view] = camera_history.update_rotation(render_frame, camera,
        {cameraView[0], cameraView[1], cameraView[2],
            cameraView[4], cameraView[5], cameraView[6],
            cameraView[8], cameraView[9], cameraView[10]});
    const uint32_t viewport = frame + 0x300 + view * 16;
    float centerX, centerY;
    std::memcpy(&centerX, rdram + (camera - kseg0) + 0x60, sizeof(float));
    std::memcpy(&centerY, rdram + (camera - kseg0) + 0x64, sizeof(float));
    const twine::render::VisibilityViewport screen{
        TWINE_MEM_H(0,viewport)/4.0, TWINE_MEM_H(2,viewport)/4.0,
        TWINE_MEM_H(8,viewport)/4.0-centerX, TWINE_MEM_H(10,viewport)/4.0-centerY};
    twine::render::begin_visibility(rdram,render_frame,camera,view,active->camera_cuts[view],lens,cameraView,&screen);
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_render_scope(uint32_t owner) try {
    scoped_owner = owner;
    scoped_command = 0;
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_render_sprite_owner(uint32_t owner) try {
    if (!native_range(owner, 1) || sprite_owner != 0) {
        throw std::runtime_error("Invalid or nested native sprite owner");
    }
    sprite_owner = owner;
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_tag_sprite(uint8_t* rdram, uint32_t command, uint32_t resource) try {
    if (!sprite_owner) { throw std::runtime_error("Missing native sprite owner"); }
    tag(rdram, command, sprite_owner, resource, 0, 9);
    sprite_owner = 0;
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_reuse_render_owner(uint32_t owner) try {
    if (!native_range(owner, 1)) { throw std::runtime_error("Invalid native recycled owner"); }

    identities.retire(owner, 1);
    camera_history.retire(owner, 1);
    twine::render::retire_visibility(owner, 1);
} catch (const std::exception& error) { fail(error.what()); }
extern "C" void twine_render_scope_matrix(uint32_t command) try { scoped_command = command; } catch (const std::exception& error) { fail(error.what()); }
extern "C" void twine_tag_scoped_model(uint8_t* rdram, uint32_t resource) try {
    if (scoped_command) { tag(rdram, scoped_command, scoped_owner, resource, 0, 5); }
    scoped_command = 0;
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_cut_render_tags(void) try {
    identities.cut();
    camera_history.reset();
    twine::render::reset_visibility();
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_retire_render_tags(uint8_t* rdram, recomp_context* ctx) try {

    const uint32_t pointer = uint32_t(ctx->r7);
    if (pointer < kseg0 + 12 || !native_range(pointer - 12, 12)) {
        throw std::runtime_error("Invalid native render owner allocation");
    }
    const uint32_t size = TWINE_MEM_W(-4, pointer);
    if (size < 12 || !native_range(pointer - 12, size)) {
        throw std::runtime_error("Invalid native render owner allocation size");
    }
    identities.retire(pointer - 12, size);
    camera_history.retire(pointer - 12, size);
    twine::render::retire_visibility(pointer - 12, size);
} catch (const std::exception& error) { fail(error.what()); }

namespace {
void write_live_render(twine::state::Writer& out, const RT64::ModernRenderState& r) {
    out.fields(r.worldActive, r.camera.valid, r.grapple.visible, r.grapple.endpointLocked, r.grapple.attachmentId);
    for (const auto& v : {r.camera.position, r.camera.forward, r.grapple.start,
            r.grapple.end, r.grapple.camera, r.grapple.forward})
        out.fields(float(v.x), float(v.y), float(v.z));
}
RT64::ModernRenderState read_live_render(twine::state::Reader& in) {
    RT64::ModernRenderState r{};
    in.fields(r.worldActive, r.camera.valid, r.grapple.visible, r.grapple.endpointLocked, r.grapple.attachmentId);
    for (auto* v : {&r.camera.position, &r.camera.forward, &r.grapple.start,
            &r.grapple.end, &r.grapple.camera, &r.grapple.forward}) {
        float x, y, z; in.fields(x, y, z);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            throw std::runtime_error("Invalid saved camera or grapple publication");
        *v = {x, y, z};
    }
    return r;
}
void validate_state_arenas(uint8_t* rdram) {
    if (active) throw std::runtime_error("A native rendering task is still being built");
    const auto prompt = twine::state::prompt_heap_owner();
    std::array owners{recomp::state::HeapAllocation{arenas[0].base, task_count * arena_size},
        recomp::state::HeapAllocation{prompt.first, prompt.second}};
    const size_t count = prompt.first ? 2 : 1;
    if (count == 2 && owners[0].address > owners[1].address) std::swap(owners[0], owners[1]);
    recomp::state::validate_native_heap(rdram, {owners.data(), count});
    for (size_t i = 0; i < arenas.size(); ++i) {
        if (arenas[i].base != arenas[0].base + i * arena_size || arenas[i].used > arena_size)
            throw std::runtime_error("Invalid render command arena ownership");
    }
}
}
struct twine::state::PreparedModernization::Impl {
    uint8_t* rdram;
    struct Arena { uint32_t base, task, used; Bytes bytes; };
    std::array<Arena, task_count> saved;
    RT64::ModernRenderState live;
};
twine::state::Bytes twine::state::capture_modernization(uint8_t* rdram) {
    validate_state_arenas(rdram);
    Writer out; out.fields(uint32_t(2), uint32_t(arenas.size()));
    write_live_render(out, RT64::getModernRenderState());
    for (const auto& arena : arenas) {
        out.fields(arena.base, arena.task, arena.used);
        out.blob({rdram + (arena.base - kseg0), arena.used});
    }
    return std::move(out.bytes);
}
twine::state::PreparedModernization::PreparedModernization(uint8_t* rdram, std::span<const uint8_t> bytes)
    : impl(std::make_unique<Impl>()) {
    validate_state_arenas(rdram);
    impl->rdram = rdram;
    Reader in(bytes);
    if (in.u32() != 2 || in.u32() != arenas.size()) throw std::runtime_error("Invalid render arena state schema");
    impl->live = read_live_render(in);
    for (size_t i = 0; i < arenas.size(); ++i) {
        auto& saved = impl->saved[i];
        in.fields(saved.base, saved.task, saved.used);
        const auto image = in.blob(arena_size);
        if (saved.base != arenas[i].base || saved.used != image.size() || (saved.used & 7U) ||
            (saved.task && !native_range(saved.task, 0x798)))
            throw std::runtime_error("Incompatible render command arena state");
        saved.bytes.assign(image.begin(), image.end());
    }
    in.end();
}
twine::state::PreparedModernization::~PreparedModernization() = default;
void twine::state::PreparedModernization::commit() noexcept {
    for (size_t i = 0; i < arenas.size(); ++i) {
        const auto& saved = impl->saved[i];
        arenas[i] = {};
        arenas[i].base = saved.base; arenas[i].task = saved.task; arenas[i].used = saved.used;
        if (!saved.bytes.empty()) std::memcpy(impl->rdram + (saved.base - kseg0), saved.bytes.data(), saved.bytes.size());
    }

    active = nullptr; identities.cut(); camera_history.reset(); render_frame = 0;
    RT64::restoreModernRenderState(impl->live);
    twine::render::reset_visibility();
    scoped_owner = scoped_command = sprite_owner = deferred_projection = 0; static_draw = {};
}
