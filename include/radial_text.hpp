#pragma once

#include "font_reconstruction.hpp"
#include "radial_menu.hpp"
#include <algorithm>
#include <string_view>

namespace twine::radial::view {
inline float label_width(size_t index, size_t count, float width, float height, float line_height, float gap) {
    const auto center = item_position(index, count);
    float available = width * 0.24f;
    for (size_t other = 0; other < count; ++other) {
        if (other == index) continue;
        const auto neighbor = item_position(other, count);
        if (std::abs(center.y - neighbor.y) * height < line_height + gap) {
            available = std::min(available, std::abs(center.x - neighbor.x) * width - gap);
        }
    }
    return std::max(1.0f, available);
}

inline unsigned glyph_index(unsigned char c) {
    return (c >= 33 && c <= 126 ? c : '?') - 33;
}

struct TextMetrics {
    float width = 0;
    float top = 0;
    float height = 0;
};

inline TextMetrics measure(const fonts::Atlas& font, std::string_view text) {
    TextMetrics result;
    float bottom = 0;
    for (unsigned char c : text) {
        if (c == ' ') { result.width += font.space_advance; continue; }
        const auto& g = font.glyphs[glyph_index(c)];
        const float y = -float(static_cast<int8_t>(g.baseline));
        result.top = std::min(result.top, y);
        bottom = std::max(bottom, y + g.height);
        result.width += std::max(0, int(g.width) + font.letter_spacing);
    }
    if (!text.empty() && text.back() != ' ') result.width -= font.letter_spacing;
    result.width = std::max(0.0f, result.width);
    result.height = bottom - result.top;
    return result;
}
}
