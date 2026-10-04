#pragma once
#include <cstdint>

namespace twine::health {

struct Recovery {
    uint32_t actor = 0;
    uint32_t inventory = 0;
    uint64_t epoch = 0;
    float health = 0;
    float armour = 0;
    uint32_t quiet_ticks = 0;
    bool damaged = false;
};
void begin_tick(uint8_t* rdram);
void finish_tick(uint8_t* rdram, bool gameplay);
void damage(uint8_t* rdram, uint32_t actor);
}
