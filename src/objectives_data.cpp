#include "objectives_overlay.hpp"
#include "native_queries.hpp"

#include <algorithm>
#include "twine_recomp.h"

namespace {
bool valid(uint32_t address, uint32_t size) {
    const uint32_t segment = address & 0xE0000000U;
    const uint32_t physical = address & 0x1FFFFFFFU;
    return segment == 0x80000000U &&
        size <= 0x800000U && physical <= 0x800000U - size;
}

bool copy_text(uint8_t* rdram, uint32_t address,
        std::array<char, twine::objectives::text_capacity>& out) {
    if (!valid(address, 2) || TWINE_MEM_BU(0, address) == 0) { return false; }

    size_t input = TWINE_MEM_BU(0, address) < 0x20U ? 2U : 0U;
    size_t output = 0;
    for (; input < out.size(); ++input) {
        if (!valid(address + uint32_t(input), 1)) { return false; }
        const uint8_t c = TWINE_MEM_BU(input, address);
        if (c == 0) { out[output] = 0; return output != 0; }
        if (output + 1 >= out.size()) { return false; }

        if (c == '\n' || c == '\r' || c == '\t') { out[output++] = ' '; }
        else if (c >= 0x20U && c < 0x7FU) { out[output++] = char(c); }
    }
    return false;
}
}

twine::objectives::Frame twine::objectives::read_frame(uint8_t* rdram, recomp_context* ctx, uint64_t epoch) {
    Frame frame{};
    frame.epoch = epoch;
    if (!rdram) { return frame; }
    native::Queries queries(rdram, ctx);
    std::array<native::Objective, maximum_rows> objectives{};
    size_t count = 0;
    if (!queries.objectives(objectives, count)) { return frame; }
    for (const auto& objective : std::span(objectives).first(count)) {

        if (objective.order < 0 || objective.state == 1) { continue; }
        Row& row = frame.rows[frame.count++];
        row.order = uint16_t(objective.order);
        row.state = objective.state;
        if (!copy_text(rdram, queries.text(objective.resource), row.text)) {
            return Frame{.epoch = epoch};
        }
    }
    std::sort(frame.rows.begin(), frame.rows.begin() + frame.count,
        [](const Row& a, const Row& b) { return a.order < b.order; });
    frame.valid = true;
    return frame;
}
