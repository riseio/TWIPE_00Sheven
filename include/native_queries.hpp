#pragma once

#include "recomp.h"
#include <span>

namespace twine::render { struct NativeCamera; }

namespace twine::native {
struct Objective {
    int16_t order;
    uint16_t resource;
    uint8_t state;
};

class Queries {
    uint8_t* rdram_;
    recomp_context context_{};
    uint32_t stack_ = 0;
public:
    Queries(uint8_t* rdram, const recomp_context* caller);
    Queries(const Queries&) = delete;
    Queries& operator=(const Queries&) = delete;
    bool camera(uint32_t player, render::NativeCamera& result);
    bool ammo(uint32_t inventory, uint8_t item);
    bool equipment(uint32_t inventory, uint8_t id);
    uint32_t text(uint16_t resource);
    bool objectives(std::span<Objective> rows, size_t& count);
};
}
