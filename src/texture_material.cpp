#include "texture_reconstruction.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace twine::textures {
Image reconstruct_material(const Image &source) {
    constexpr unsigned scale = 9, border = 2 * scale;
    auto original = reconstruct(source, scale);
    auto detailed = source;
    const auto luma = [&](unsigned x, unsigned y) {
        const auto p = (size_t(y) * source.width + x) * 4;
        return (54 * int(source.rgba[p]) + 183 * int(source.rgba[p + 1]) +
                19 * int(source.rgba[p + 2]) + 128) / 256;
    };

    for (unsigned y = 2; y + 2 < source.height; ++y)
        for (unsigned x = 2; x + 2 < source.width; ++x) {
            int sum = 0, broad = 0;
            std::array<int, 3> low{255, 255, 255}, high{};
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) {
                    const unsigned sx = unsigned(int(x) + i), sy = unsigned(int(y) + j);
                    sum += luma(sx, sy) * (i ? 1 : 2) * (j ? 1 : 2);
                    const auto p = (size_t(sy) * source.width + sx) * 4;
                    for (unsigned c = 0; c < 3; ++c) {
                        low[c] = std::min(low[c], int(source.rgba[p + c]));
                        high[c] = std::max(high[c], int(source.rgba[p + c]));
                    }
                }
            constexpr std::array<int, 5> weights{1, 4, 6, 4, 1};
            for (int j = -2; j <= 2; ++j)
                for (int i = -2; i <= 2; ++i)
                    broad += luma(unsigned(int(x) + i), unsigned(int(y) + j)) * weights[i + 2] * weights[j + 2];

            const int fine = 16 * luma(x, y) - sum;
            const int medium = 16 * sum - broad;
            const int correction = 48 * fine + 5 * medium;
            const int delta = std::clamp((std::abs(correction) + 512) / 1024 *
                                         (correction < 0 ? -1 : 1), -20, 20);
            const auto p = (size_t(y) * source.width + x) * 4;

            for (unsigned c = 0; c < 3; ++c) {
                const int allowance = std::min(2, (high[c] - low[c]) / 16);
                detailed.rgba[p + c] = uint8_t(std::clamp(int(source.rgba[p + c]) + delta,
                    std::max(0, low[c] - allowance), std::min(255, high[c] + allowance)));
            }
        }
    const auto sharp = reconstruct(detailed, scale);

    for (unsigned y = border; y + border < original.height; ++y)
        for (unsigned x = border; x + border < original.width; ++x) {
            const auto distance = std::min({x, y, original.width - 1 - x, original.height - 1 - y});
            float amount = std::min(float(distance - border) / scale, 1.0f);
            amount = amount * amount * (3 - 2 * amount);
            const auto p = (size_t(y) * original.width + x) * 4;
            for (unsigned c = 0; c < 3; ++c)
                original.rgba[p + c] = uint8_t(std::lround(original.rgba[p + c] +
                    amount * (int(sharp.rgba[p + c]) - int(original.rgba[p + c]))));
        }
    return original;
}
}
