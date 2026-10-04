#include "font_reconstruction.hpp"

#include <algorithm>
#include <cmath>
#include "font_magnification.hpp"
#include <stdexcept>
#include "twine_recomp.h"

namespace {
bool range(uint32_t address, uint32_t bytes) {
    return address >= 0x80000000U && address < 0x80800000U &&
        bytes <= 0x80800000U - address;
}
struct Bounds { int left, top, right, bottom; };
float cubic(float a, float b, float c, float d, float t) {
    return b + 0.5f * t * (c - a + t *
        (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
}

}

bool twine::fonts::read_atlases(uint8_t* rdram, uint32_t bank, Atlases& out) {
    if (!rdram || (bank & 3U) || !range(bank, 0x30) ||
            uint32_t(TWINE_MEM_W(0, 0x800C0A24U)) != bank) { return false; }
    const uint32_t records = TWINE_MEM_W(0x18, bank);
    const uint32_t count = TWINE_MEM_W(0x1C, bank);
    if (count != 21 || (records & 3U) || !range(records, count * 12)) { return false; }
    constexpr std::array<uint32_t, font_count> indices{4, 9, 10, 11, 20};
    for (size_t i = 0; i < font_count; ++i) {
        const uint32_t definition = 0x800B5A98U + uint32_t(i) * 20;
        const uint32_t index = TWINE_MEM_W(4, definition);
        const uint32_t glyphs = TWINE_MEM_W(8, definition);
        out[i].space_advance = TWINE_MEM_BU(16, definition);
        out[i].letter_spacing = TWINE_MEM_B(17, definition);
        if (index != indices[i] || !range(glyphs, glyph_count * 5)) { return false; }
        const uint32_t record = records + index * 12;
        const uint32_t data = TWINE_MEM_W(8, record);
        if (TWINE_MEM_HU(0, record) != 0x0800 ||
                TWINE_MEM_HU(2, record) != width - 1 ||
                TWINE_MEM_HU(4, record) != height - 1 ||
                TWINE_MEM_HU(6, record) != 0x0100 ||
                !range(data, width * height / 2)) { return false; }
        for (size_t n = 0; n < out[i].packed.size(); ++n) {
            out[i].packed[n] = rdram[((data & 0x1FFFFFFFU) + n) ^ 3U];
        }
        for (size_t n = 0; n < glyph_count; ++n) {
            const uint32_t p = glyphs + uint32_t(n) * 5;
            auto& g = out[i].glyphs[n];
            g = {TWINE_MEM_BU(0, p), TWINE_MEM_BU(1, p),
                TWINE_MEM_BU(2, p), TWINE_MEM_BU(3, p), TWINE_MEM_BU(4, p)};
            if (unsigned(g.x) + g.width > width ||
                    unsigned(g.y) + g.height > height) { return false; }
        }
    }
    return true;
}

std::vector<uint8_t> twine::fonts::reconstruct(const Atlas& atlas) {
    std::array<Bounds, width * height> bounds;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            bounds[y * width + x] = {0, 0, int(width), int(height)};
        }
    }

    for (const auto& g : atlas.glyphs) {
        if (unsigned(g.x) + g.width > width || unsigned(g.y) + g.height > height) {
            throw std::runtime_error("Native font glyph exceeds its atlas");
        }
        for (unsigned y = g.y; y < unsigned(g.y) + g.height; ++y) {
            for (unsigned x = g.x; x < unsigned(g.x) + g.width; ++x) {
                auto& b = bounds[y * width + x];
                b.left = std::max(b.left, int(g.x));
                b.top = std::max(b.top, int(g.y));
                b.right = std::min(b.right, int(g.x) + g.width);
                b.bottom = std::min(b.bottom, int(g.y) + g.height);
            }
        }
    }
    std::vector<uint8_t> rgba(width * height * scale * scale * 4);
    constexpr unsigned contour_scale = 2;
    std::vector<uint8_t> pixels(width * height);
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = ((i & 1) ? atlas.packed[i / 2] & 15 : atlas.packed[i / 2] >> 4) * 17;
    }

    for (unsigned factor = 1; factor < contour_scale; factor *= 2) {
        const unsigned stride = width * factor;
        std::vector<uint8_t> next(pixels.size() * 4);
        for (unsigned y = 0; y < height * factor; ++y) {
            for (unsigned x = 0; x < stride; ++x) {
                const auto& b = bounds[(y / factor) * width + x / factor];
                auto sample = [&](int px, int py) -> int {
                    return pixels[std::clamp(py, b.top * int(factor), b.bottom * int(factor) - 1) * stride +
                        std::clamp(px, b.left * int(factor), b.right * int(factor) - 1)];
                };
                const auto sub = detail::magnify(int(x), int(y), sample);
                const size_t at = size_t(y * 2) * stride * 2 + x * 2;
                next[at] = uint8_t(sub[0]); next[at + 1] = uint8_t(sub[1]);
                next[at + stride * 2] = uint8_t(sub[2]); next[at + stride * 2 + 1] = uint8_t(sub[3]);
            }
        }
        pixels = std::move(next);
    }
    const auto& coverage = pixels;
    for (unsigned y = 0; y < height * scale; ++y) {
        for (unsigned x = 0; x < width * scale; ++x) {
            const auto& b = bounds[(y / scale) * width + x / scale];
            const float sx = (float(x) + 0.5f) * contour_scale / scale - 0.5f;
            const float sy = (float(y) + 0.5f) * contour_scale / scale - 0.5f;
            const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
            const float fx = sx - ix, fy = sy - iy;
            auto sample = [&](int px, int py) -> float {
                return coverage[std::clamp(py, b.top * int(contour_scale), b.bottom * int(contour_scale) - 1) * width * contour_scale +
                    std::clamp(px, b.left * int(contour_scale), b.right * int(contour_scale) - 1)];
            };
            std::array<float, 4> rows{};
            for (int row = 0; row < 4; ++row) {
                rows[row] = cubic(sample(ix - 1, iy + row - 1), sample(ix, iy + row - 1),
                    sample(ix + 1, iy + row - 1), sample(ix + 2, iy + row - 1), fx);
            }
            const std::array<float, 4> central{
                sample(ix, iy), sample(ix + 1, iy), sample(ix, iy + 1), sample(ix + 1, iy + 1)};
            const auto [low, high] = std::minmax_element(central.begin(), central.end());
            const auto value = uint8_t(std::lround(std::clamp(
                cubic(rows[0], rows[1], rows[2], rows[3], fy), *low, *high)));
            std::fill_n(rgba.begin() + (size_t(y) * width * scale + x) * 4, 4, value);
        }
    }
    return rgba;
}

std::vector<uint8_t> twine::fonts::encode_dds(std::span<const uint8_t> rgba) {
    if (rgba.size() != width * height * scale * scale * 4) {
        throw std::runtime_error("Unexpected enhanced font image size");
    }
    std::vector<uint8_t> data(128 + rgba.size(), 0);
    auto word = [&](size_t offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) { data[offset + i] = uint8_t(value >> (i * 8)); }
    };
    word(0, 0x20534444); word(4, 124); word(8, 0x100F);
    word(12, height * scale); word(16, width * scale); word(20, width * scale * 4);
    word(76, 32); word(80, 0x41); word(88, 32);
    word(92, 0xFF); word(96, 0xFF00); word(100, 0xFF0000); word(104, 0xFF000000);
    word(108, 0x1000);
    std::copy(rgba.begin(), rgba.end(), data.begin() + 128);
    return data;
}
