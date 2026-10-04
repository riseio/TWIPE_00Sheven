#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "recomp.h"

namespace twine::objectives {
inline constexpr std::size_t maximum_rows = 64;
inline constexpr std::size_t text_capacity = 512;
struct Row {
    std::array<char, text_capacity> text{};
    uint16_t order = 0;
    uint8_t state = 0;
    bool operator==(const Row&) const = default;
};
struct Frame {
    std::array<Row, maximum_rows> rows{};
    uint64_t epoch = 0;
    uint16_t count = 0;
    bool valid = false;
    bool operator==(const Frame&) const = default;
};

class Toggle {
    uint64_t epoch_ = 0;
    bool held_ = false;
    bool visible_ = false;
public:
    bool update(bool available, bool down, uint64_t epoch) {
        const bool edge = down && !held_;
        held_ = down;
        if (epoch_ != epoch || !available) {
            epoch_ = epoch;
            visible_ = false;
        }
        else if (edge) { visible_ = !visible_; }
        return visible_;
    }
};

Frame read_frame(uint8_t* rdram, recomp_context* ctx, uint64_t epoch);
void update_input(bool available, bool down, uint64_t epoch);
void publish(uint8_t* rdram, recomp_context* ctx, bool gameplay);
void update_ui();
bool visible();
bool displayed();
uint16_t displayed_count();
}
