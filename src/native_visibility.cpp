#include "native_visibility.hpp"
#include "twine_recomp.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <optional>
namespace ultramodern { float get_aspect_ratio_scale(); }

namespace {
constexpr uint32_t base = 0x80000000U;
void require_range(uint32_t address, uint32_t size) {
    if (address < base || address >= base + 0x800000U || size > base + 0x800000U-address) {
        throw std::runtime_error("Native visibility metadata is outside RDRAM");
    }
}
float read_float(uint8_t* rdram, uint32_t address) {
    require_range(address, 4);
    const float result = std::bit_cast<float>(uint32_t(TWINE_MEM_W(0, address)));
    if (!std::isfinite(result)) { throw std::runtime_error("Nonfinite native visibility coordinate"); }
    return result;
}
void write_float(uint8_t* rdram, uint32_t address, double value) {
    require_range(address, 4);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
        throw std::runtime_error("Native visibility result exceeds float range");
    }
    TWINE_MEM_W(0, address) = std::bit_cast<uint32_t>(float(value));
}
using namespace twine::render;
VisibilityRect read_rect(uint8_t* rdram, uint32_t address) {
    return {read_float(rdram,address),read_float(rdram,address+4),
        read_float(rdram,address+8),read_float(rdram,address+12)};
}
struct CameraEntry {
    uint64_t frame = 0;
    uint32_t camera = 0, controller = 0, view = 0;
    Matrix lens{}, transform{}, previousLens{}, previousTransform{};
    bool cut = true;
    std::optional<VisibilitySweep> sweep;
};
std::array<CameraEntry, 24> cameras;
CameraEntry* active = nullptr;

template<class T, size_t Capacity> class History {
    struct Entry {
        uint32_t key = 0;
        uint64_t frame = 0;
        T previous{}, current{};
    };
    std::array<Entry, Capacity> entries{};
public:
    const Entry& update(uint32_t key, uint64_t frame, const T& value) {
        Entry* available = nullptr;
        for (size_t i = 0; i < Capacity; ++i) {
            auto& entry = entries[((key >> 2) + i) % Capacity];
            if (entry.key == key) {
                if (entry.frame != frame) {
                    entry.previous = entry.frame + 1 == frame ? entry.current : value;
                    entry.frame = frame;
                }
                entry.current = value;
                return entry;
            }
            if (!available && (entry.key <= 1 || entry.frame + 1 < frame)) { available = &entry; }
            if (!entry.key) { break; }
        }
        if (!available) { throw std::runtime_error("Native visibility history capacity exceeded"); }
        *available = {key, frame, value, value};
        return *available;
    }
    void retire(uint32_t address, uint32_t size, bool center = false) {
        const auto contains = [=](uint32_t pointer) {
            return pointer >= address && uint64_t(pointer) - address < size;
        };
        for (auto& entry : entries) {

            if (entry.key > 1 && (contains(entry.key) ||
                    (center && (contains(entry.key - 0x24) || contains(entry.key - 0x40))))) {
                entry = {}; entry.key = 1;
            }
        }
    }
    void reset() { entries = {}; }
};
struct Sphere { std::array<float, 3> center{}; float radius = 0; };
History<Sphere, 8192> spheres;
History<bool, 1024> doors;
[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "Native visibility failed: %s\n", message);

    std::abort();
}
}

void twine::render::begin_visibility(uint8_t* rdram, uint64_t frame, uint32_t camera,
        uint32_t view, bool cut, Matrix lens, const Matrix& cameraView, const VisibilityViewport* screen) {
    require_range(camera, 0x14C);
    CameraEntry* entry = nullptr;
    for (auto& candidate : cameras) {
        if (candidate.camera == camera && candidate.view == view) { entry = &candidate; break; }
        if (!entry && (!candidate.camera || candidate.frame+1 < frame)) { entry = &candidate; }
    }
    if (!entry) { throw std::runtime_error("Native visibility camera capacity exceeded"); }
    const auto controller = uint32_t(TWINE_MEM_W(0x138, camera));
    const bool sameFrame = entry->camera == camera && entry->view == view &&
        entry->controller == controller && entry->frame == frame;
    const bool continuous = !cut && entry->camera == camera && entry->view == view &&
        entry->controller == controller && entry->frame+1 == frame;
    if (!sameFrame) {
        entry->previousLens = continuous ? entry->lens : lens;
        entry->previousTransform = continuous ? entry->transform : cameraView;
    }
    Matrix previousLens = cut ? lens : entry->previousLens;
    const Matrix previousView = cut ? cameraView : entry->previousTransform;
    entry->lens = lens;
    const float aspect = ultramodern::get_aspect_ratio_scale();
    if (!std::isfinite(aspect) || aspect <= 0) { throw std::runtime_error("Invalid visibility aspect ratio"); }
    for (size_t i = 0; i < 16; i += 4) { lens[i] /= aspect; previousLens[i] /= aspect; }
    const auto viewport = read_rect(rdram, camera+0x50);
    if (viewport.empty()) { throw std::runtime_error("Empty native visibility viewport"); }
    const float scale = read_float(rdram, camera+0x4C);
    const VisibilityViewport fallback{(viewport.right-viewport.left)*scale*0.5,
        (viewport.bottom-viewport.top)*scale*0.5,0,0};
    entry->sweep.emplace(previousView, cameraView, previousLens, lens, viewport, screen ? *screen : fallback);
    entry->camera = camera; entry->controller = controller; entry->view = view;
    entry->frame = frame; entry->transform = cameraView;
    entry->cut = cut;
    active = entry;
}

void twine::render::reset_visibility() {
    cameras = {}; active = nullptr; spheres.reset(); doors.reset();
}
void twine::render::retire_visibility(uint32_t address, uint32_t size) {
    const auto contains = [=](uint32_t pointer) { return pointer >= address && uint64_t(pointer)-address < size; };
    for (auto& entry : cameras) {
        if (contains(entry.camera) || contains(entry.controller)) {
            if (active == &entry) { active = nullptr; }
            entry = {};
        }
    }
    spheres.retire(address, size, true);
    doors.retire(address, size);
}

extern "C" void twine_visibility_door(uint8_t*, recomp_context* ctx) try {
    if (!active || !active->sweep) { throw std::runtime_error("Door traversal has no camera interval"); }
    const bool open = ctx->r2 != 0;
    const auto& history = doors.update(uint32_t(ctx->r4), active->frame, open);
    ctx->r2 = open || (!active->cut && history.previous);
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_visibility_sphere(uint8_t* rdram, recomp_context* ctx) try {
    if (!active || !active->sweep || active->camera != uint32_t(ctx->r7)) {
        throw std::runtime_error("Object culling has no matching render camera");
    }
    const uint32_t center = uint32_t(ctx->r4), room = uint32_t(ctx->r6);
    require_range(center, 12); require_range(room, 0x24);
    Sphere sphere;
    for (uint32_t i = 0; i < 3; ++i) { sphere.center[i] = read_float(rdram, center + i * 4); }
    sphere.radius = std::bit_cast<float>(uint32_t(ctx->r5));
    if (!std::isfinite(sphere.radius) || sphere.radius < 0) { throw std::runtime_error("Invalid object radius"); }
    const auto& history = spheres.update(center, active->frame, sphere);
    const auto& previous = active->cut ? sphere : history.previous;
    const float radius = std::max(previous.radius, sphere.radius);
    const auto bounds = read_rect(rdram, uint32_t(TWINE_MEM_W(0x20, room)));
    const float scale = read_float(rdram, active->camera + 0x4C);

    const bool sizeVisible = radius * scale <= 0.5f ||
        active->sweep->radiusPixelsMaximum(previous.center, sphere.center, radius) >= 2;
    ctx->r2 = sizeVisible && active->sweep->sphere(previous.center, sphere.center, radius, bounds);
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_visibility_distance(uint8_t* rdram, recomp_context* ctx) try {
    if (!active || !active->sweep) { throw std::runtime_error("Room distance culling has no camera interval"); }
    std::array<float, 3> center;
    for (uint32_t i = 0; i < 3; ++i) { center[i] = read_float(rdram, uint32_t(ctx->r16) + 0x40 + i * 4); }
    ctx->f0.fl = std::min(ctx->f0.fl, float(active->sweep->distanceMinimum(center)));
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_visibility_plane(uint8_t* rdram, recomp_context* ctx) try {
    if (!active || !active->sweep) { throw std::runtime_error("Portal traversal has no camera interval"); }
    const uint32_t portal = uint32_t(ctx->r22);
    require_range(portal, 0x1C);
    std::array<float,4> plane;
    for (size_t i=0; i<4; ++i) { plane[i]=read_float(rdram,portal+0xC+uint32_t(i)*4); }

    const double limit = std::numeric_limits<float>::max();
    ctx->f4.fl = std::max(ctx->f4.fl, float(std::clamp(active->sweep->planeMaximum(plane),-limit,limit)));
} catch (const std::exception& error) { fail(error.what()); }

extern "C" void twine_visibility_triangle(uint8_t* rdram, recomp_context* ctx) try {
    if (!active || !active->sweep) { throw std::runtime_error("Portal triangle has no camera interval"); }
    const uint32_t model=uint32_t(ctx->r20), triangle=uint32_t(ctx->r23);
    require_range(model,0x4C);
    const uint32_t count=TWINE_MEM_HU(0x40,model), vertexCount=TWINE_MEM_HU(0x42,model);
    if (triangle>=count || !vertexCount) { throw std::runtime_error("Invalid native portal triangle count"); }
    const uint32_t indices=TWINE_MEM_W(0x3C,model), vertices=TWINE_MEM_W(0x34,model);
    const uint32_t stride=(TWINE_MEM_BU(0x48,model)&4) ? 2 : 1;
    require_range(indices,count*3*stride);
    require_range(vertices,vertexCount*12);
    std::array<std::array<float,3>,3> points;
    for (uint32_t v=0;v<3;++v) {
        const uint32_t offset=(triangle*3+v)*stride;
        const uint32_t index=stride==2 ? TWINE_MEM_HU(offset,indices) : TWINE_MEM_BU(offset,indices);
        if (index>=vertexCount) { throw std::runtime_error("Native portal index exceeds its vertex array"); }
        for (uint32_t c=0;c<3;++c) { points[v][c]=read_float(rdram,vertices+index*12+c*4); }
    }
    const auto bounds=active->sweep->triangle(points,read_rect(rdram,uint32_t(ctx->r30)));
    if (!bounds.empty()) {
        const uint32_t resultAddress=uint32_t(ctx->r29)+0x90;
        auto result=read_rect(rdram,resultAddress);
        result.include(bounds);
        write_float(rdram,resultAddress,result.left); write_float(rdram,resultAddress+4,result.right);
        write_float(rdram,resultAddress+8,result.top); write_float(rdram,resultAddress+12,result.bottom);
        TWINE_MEM_B(0xA7,ctx->r29)=1;
    }

    const uint32_t scratch=uint32_t(TWINE_MEM_W(0xE0,ctx->r29));

    write_float(rdram,scratch+0x50,ctx->f4.fl);
    const float depth=std::max({read_float(rdram,scratch+8),
        read_float(rdram,scratch+0x2C),read_float(rdram,scratch+0x50)});
    for (uint32_t i=0;i<4;++i) { write_float(rdram,scratch+i*0x24+8,depth); }
} catch (const std::exception& error) { fail(error.what()); }
